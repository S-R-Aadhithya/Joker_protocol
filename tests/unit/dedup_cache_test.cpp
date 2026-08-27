#include <gtest/gtest.h>
#include "joker/dedup_cache.hpp"
#include <thread>

using namespace joker;
using namespace std::chrono_literals;

TEST(DedupCacheTest, BasicInsertAndContains) {
    DedupCache cache(10, 1000ms);
    EXPECT_FALSE(cache.Contains(1));
    cache.Insert(1);
    EXPECT_TRUE(cache.Contains(1));
}

TEST(DedupCacheTest, EvictsOldestWhenAtCapacity) {
    DedupCache cache(2, 1000ms);
    cache.Insert(1);
    cache.Insert(2);
    EXPECT_TRUE(cache.Contains(1));
    EXPECT_TRUE(cache.Contains(2));

    // Inserting 3 should evict 1 (oldest)
    cache.Insert(3);
    EXPECT_FALSE(cache.Contains(1));
    EXPECT_TRUE(cache.Contains(2));
    EXPECT_TRUE(cache.Contains(3));
}

TEST(DedupCacheTest, InsertMovesToMostRecentlyUsed) {
    DedupCache cache(2, 1000ms);
    cache.Insert(1);
    cache.Insert(2);
    
    // Touch 1 to make it most recently used
    cache.Insert(1);

    // Inserting 3 should evict 2 (oldest now)
    cache.Insert(3);
    
    EXPECT_TRUE(cache.Contains(1));
    EXPECT_FALSE(cache.Contains(2));
    EXPECT_TRUE(cache.Contains(3));
}

TEST(DedupCacheTest, EvictsExpired) {
    DedupCache cache(10, 50ms);
    cache.Insert(1);
    
    std::this_thread::sleep_for(60ms);
    
    // 1 is expired but Contains doesn't actively sweep
    // We call EvictExpired to sweep
    cache.EvictExpired(std::chrono::steady_clock::now());
    EXPECT_FALSE(cache.Contains(1));
}
