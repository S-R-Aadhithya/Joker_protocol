#include <gtest/gtest.h>
#include "joker/neighbor.hpp"
#include <thread>

using namespace joker;
using namespace std::chrono_literals;

TEST(NeighborTest, UpdateAndLookup) {
    NeighborTable table;
    MacAddress mac({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff});
    
    EXPECT_FALSE(table.Lookup(mac).has_value());
    
    NeighborEntry entry;
    entry.mac = mac;
    entry.last_seen = std::chrono::steady_clock::now();
    entry.tq_local = 200;
    
    table.Update(entry);
    
    auto result = table.Lookup(mac);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->tq_local, 200);
}

TEST(NeighborTest, ExpireStale) {
    NeighborTable table;
    MacAddress mac1({0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    MacAddress mac2({0x11, 0x22, 0x33, 0x44, 0x55, 0x66});
    
    NeighborEntry entry1, entry2;
    entry1.mac = mac1;
    entry1.last_seen = std::chrono::steady_clock::now();
    
    entry2.mac = mac2;
    entry2.last_seen = std::chrono::steady_clock::now() + 100ms;
    
    table.Update(entry1);
    table.Update(entry2);
    
    std::this_thread::sleep_for(50ms);
    
    // Expire anything older than 40ms.
    // entry1 is ~50ms old, entry2 is in the future.
    table.ExpireStale(std::chrono::steady_clock::now(), 40ms);
    
    EXPECT_FALSE(table.Lookup(mac1).has_value());
    EXPECT_TRUE(table.Lookup(mac2).has_value());
}

TEST(NeighborTest, AllFresh) {
    NeighborTable table;
    MacAddress mac1({0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    
    NeighborEntry entry1;
    entry1.mac = mac1;
    entry1.last_seen = std::chrono::steady_clock::now();
    
    table.Update(entry1);
    
    std::this_thread::sleep_for(50ms);
    
    auto fresh_50 = table.AllFresh(std::chrono::steady_clock::now(), 10ms);
    EXPECT_TRUE(fresh_50.empty());
    
    auto fresh_long = table.AllFresh(std::chrono::steady_clock::now(), 1000ms);
    EXPECT_EQ(fresh_long.size(), 1);
}
