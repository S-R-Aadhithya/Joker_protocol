#include <gtest/gtest.h>
#include "joker/candidate.hpp"
#include "joker/mac_address.hpp"
#include "joker/neighbor.hpp"
#include "joker/config.hpp"
#include <chrono>

using namespace joker;

TEST(CandidateTest, SelectCandidatesBasic) {
    NeighborTable table;
    auto now = std::chrono::steady_clock::now();

    MacAddress dest = MacAddress::Parse("00:11:22:33:44:55").value();

    // Neighbor 1: Good LQ
    NeighborEntry n1;
    n1.mac = MacAddress::Parse("AA:BB:CC:DD:EE:01").value();
    n1.last_seen = now;
    n1.reachable_to_dest = true;
    n1.tq_local = 250;
    n1.tq_recv = 250;
    n1.f_asym = 1.0;
    n1.hop_penalty = 1.0;
    n1.rx_power_dbm = -40.0; // High signal -> FM > 20 -> DP=5
    table.Update(n1);

    // Neighbor 2: Poor LQ
    NeighborEntry n2;
    n2.mac = MacAddress::Parse("AA:BB:CC:DD:EE:02").value();
    n2.last_seen = now;
    n2.reachable_to_dest = true;
    n2.tq_local = 100;
    n2.tq_recv = 100;
    n2.f_asym = 1.0;
    n2.hop_penalty = 1.0;
    n2.rx_power_dbm = -60.0; // FM=14 -> DP=3
    table.Update(n2);

    // Neighbor 3: Stale
    NeighborEntry n3;
    n3.mac = MacAddress::Parse("AA:BB:CC:DD:EE:03").value();
    n3.last_seen = now - std::chrono::seconds(10); // Exceeds default 6s expiry
    n3.reachable_to_dest = true;
    n3.tq_local = 255;
    n3.tq_recv = 255;
    n3.f_asym = 1.0;
    n3.hop_penalty = 1.0;
    n3.rx_power_dbm = -30.0;
    table.Update(n3);

    // Neighbor 4: Unreachable
    NeighborEntry n4;
    n4.mac = MacAddress::Parse("AA:BB:CC:DD:EE:04").value();
    n4.last_seen = now;
    n4.reachable_to_dest = false;
    n4.tq_local = 255;
    n4.tq_recv = 255;
    n4.f_asym = 1.0;
    n4.hop_penalty = 1.0;
    n4.rx_power_dbm = -30.0;
    table.Update(n4);

    Config config;
    config.candidate_count = 2;
    config.sensitivity_dbm = -74.0;
    config.neighbor_expiry_ms = 6000;
    config.tq_max = 255;

    auto candidates = select_candidates(dest, table, config.candidate_count, config);

    // Should only select n1 and n2 (n3 is stale, n4 is unreachable)
    ASSERT_EQ(candidates.size(), 2);

    // n1 should have higher LQ than n2
    EXPECT_EQ(candidates[0].mac.ToString(), "aa:bb:cc:dd:ee:01");
    EXPECT_EQ(candidates[0].priority, 1);
    EXPECT_EQ(candidates[1].mac.ToString(), "aa:bb:cc:dd:ee:02");
    EXPECT_EQ(candidates[1].priority, 2);
    EXPECT_GT(candidates[0].lq, candidates[1].lq);
}

TEST(CandidateTest, TieBreakDeterminism) {
    NeighborTable table;
    auto now = std::chrono::steady_clock::now();

    MacAddress dest = MacAddress::Parse("00:11:22:33:44:55").value();

    // Identical metrics, different MACs
    NeighborEntry n1, n2, n3;
    
    n1.mac = MacAddress::Parse("00:00:00:00:00:03").value();
    n2.mac = MacAddress::Parse("00:00:00:00:00:01").value();
    n3.mac = MacAddress::Parse("00:00:00:00:00:02").value();

    n1.last_seen = now; n2.last_seen = now; n3.last_seen = now;
    n1.reachable_to_dest = true; n2.reachable_to_dest = true; n3.reachable_to_dest = true;
    n1.tq_local = 200; n2.tq_local = 200; n3.tq_local = 200;
    n1.tq_recv = 200; n2.tq_recv = 200; n3.tq_recv = 200;
    n1.f_asym = 1.0; n2.f_asym = 1.0; n3.f_asym = 1.0;
    n1.hop_penalty = 1.0; n2.hop_penalty = 1.0; n3.hop_penalty = 1.0;
    n1.rx_power_dbm = -50.0; n2.rx_power_dbm = -50.0; n3.rx_power_dbm = -50.0;

    table.Update(n1);
    table.Update(n2);
    table.Update(n3);

    Config config;
    config.candidate_count = 3;

    auto candidates = select_candidates(dest, table, config.candidate_count, config);

    ASSERT_EQ(candidates.size(), 3);
    EXPECT_DOUBLE_EQ(candidates[0].lq, candidates[1].lq);
    EXPECT_DOUBLE_EQ(candidates[1].lq, candidates[2].lq);

    // Sort order should be ascending MAC address
    EXPECT_EQ(candidates[0].mac.ToString(), "00:00:00:00:00:01");
    EXPECT_EQ(candidates[1].mac.ToString(), "00:00:00:00:00:02");
    EXPECT_EQ(candidates[2].mac.ToString(), "00:00:00:00:00:03");
}

TEST(CandidateTest, TruncateCount) {
    NeighborTable table;
    auto now = std::chrono::steady_clock::now();
    MacAddress dest = MacAddress::Parse("00:11:22:33:44:55").value();

    for (int i = 0; i < 5; ++i) {
        NeighborEntry n;
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "00:00:00:00:00:%02X", i + 1);
        n.mac = MacAddress::Parse(mac_str).value();
        n.last_seen = now;
        n.reachable_to_dest = true;
        n.tq_local = 200;
        n.tq_recv = 200;
        n.f_asym = 1.0;
        n.hop_penalty = 1.0;
        n.rx_power_dbm = -50.0;
        table.Update(n);
    }

    Config config;
    config.candidate_count = 2; // Should truncate from 5 to 2

    auto candidates = select_candidates(dest, table, config.candidate_count, config);

    ASSERT_EQ(candidates.size(), 2);
    EXPECT_EQ(candidates[0].priority, 1);
    EXPECT_EQ(candidates[1].priority, 2);
}
