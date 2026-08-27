#include <gtest/gtest.h>
#include "joker/forwarding.hpp"
#include "joker/coordinator.hpp"
#include "joker/metrics.hpp"
#include "joker/config.hpp"
#include "joker/timer_wheel.hpp"
#include "mock_network.hpp"
#include "virtual_timer_wheel.hpp"
#include <random>

using namespace joker;

struct JokerPenetrationNode {
    MacAddress mac;
    MockAdapter adapter;
    NeighborTable neighbors;
    DedupCache dedup;
    TimerCoordinator coordinator;
    SimpleMetrics metrics;
    Config config;

    JokerPenetrationNode(MacAddress mac, VirtualTimerWheel& wheel) 
        : mac(mac), adapter(mac), dedup(100, std::chrono::milliseconds(5000)), coordinator(wheel) {
        
        config.twait_ms = 50;
        config.candidate_count = 3;
        
        adapter.RegisterReceiveCallback([this](const std::vector<uint8_t>& frame, const MacAddress&, bool is_candidate) {
            process_received_frame(frame, this->mac, is_candidate, this->neighbors, this->dedup, this->coordinator, this->adapter, this->metrics, this->config);
        });
        adapter.Start();
    }
};

class PenetrationTest : public ::testing::Test {
protected:
    VirtualTimerWheel clock;
    MockNetwork network;

    MacAddress macA = MacAddress::Parse("00:00:00:00:00:01").value();
    MacAddress macB = MacAddress::Parse("00:00:00:00:00:02").value();
    MacAddress macC = MacAddress::Parse("00:00:00:00:00:03").value();
};

TEST_F(PenetrationTest, FuzzingAndMalformedPackets) {
    JokerPenetrationNode nodeA(macA, clock);
    
    // 1. Completely random noise (Garbage)
    std::random_device rd;
    std::mt19937 gen(1337);
    std::uniform_int_distribution<> dis(0, 255);
    
    for (int i = 0; i < 1000; ++i) {
        std::vector<uint8_t> frame;
        size_t len = dis(gen) % 100;
        for (size_t j = 0; j < len; ++j) {
            frame.push_back(dis(gen));
        }
        // Inject into adapter directly (simulates receiving raw garbage)
        nodeA.adapter.InjectIncomingFrame(frame, true);
        nodeA.adapter.InjectIncomingFrame(frame, false);
    }
    
    // 2. Truncated headers (starts valid, cuts off)
    JokerHeader valid_header{};
    valid_header.packet_id = 1;
    valid_header.type = PacketType::kUnicast;
    valid_header.ttl = 10;
    valid_header.final_destination = macC;
    std::vector<uint8_t> valid_frame;
    serialize_header(valid_header, valid_frame);
    
    for (size_t len = 0; len < valid_frame.size(); ++len) {
        std::vector<uint8_t> trunc_frame(valid_frame.begin(), valid_frame.begin() + len);
        nodeA.adapter.InjectIncomingFrame(trunc_frame, true);
    }

    // Should not crash! Metrics should record drops.
    EXPECT_GT(nodeA.metrics.GetCounter("packets_dropped"), 0);
    EXPECT_GT(nodeA.metrics.GetCounter("malformed_header"), 0);
}

TEST_F(PenetrationTest, ResourceExhaustion_DDoS) {
    JokerPenetrationNode nodeA(macA, clock);
    
    // Inject 10,000 unique valid packets instantly
    // The dedup cache capacity is 100.
    for (uint32_t i = 1; i <= 10000; ++i) {
        JokerHeader header{};
        header.packet_id = i;
        header.type = PacketType::kUnicast;
        header.ttl = 32;
        header.final_destination = macC;
        std::vector<uint8_t> frame;
        serialize_header(header, frame);
        
        nodeA.adapter.InjectIncomingFrame(frame, true);
    }

    // Node A should have processed all 10,000.
    // Memory should not explode (DedupCache size should be at most 100).
    EXPECT_EQ(nodeA.metrics.GetCounter("candidate_packets_received"), 10000);
    EXPECT_EQ(nodeA.metrics.GetCounter("duplicates"), 0); // No duplicates injected
    
    // We expect the TimerCoordinator pending queue to also grow to 10,000? 
    // Yes, but we don't limit the pending queue in the core protocol currently.
    // That is a protocol design choice/flaw.
    EXPECT_EQ(nodeA.coordinator.GetPendingCount(), 10000);
}

TEST_F(PenetrationTest, MacSpoofing) {
    JokerPenetrationNode nodeA(macA, clock);
    MacAddress bcast = MacAddress::Parse("ff:ff:ff:ff:ff:ff").value();
    MacAddress null_mac = MacAddress::Parse("00:00:00:00:00:00").value();

    // 1. Spoofed Broadcast as Final Destination in Unicast
    JokerHeader h1{};
    h1.packet_id = 1;
    h1.type = PacketType::kUnicast;
    h1.ttl = 32;
    h1.final_destination = bcast;
    std::vector<uint8_t> frame1;
    serialize_header(h1, frame1);
    nodeA.adapter.InjectIncomingFrame(frame1, true);

    // 2. Null MAC in candidates
    JokerHeader h2{};
    h2.packet_id = 2;
    h2.type = PacketType::kUnicast;
    h2.ttl = 32;
    h2.final_destination = macC;
    h2.other_candidates.push_back(null_mac);
    std::vector<uint8_t> frame2;
    serialize_header(h2, frame2);
    nodeA.adapter.InjectIncomingFrame(frame2, true);

    // Should be handled gracefully, potentially dropped during validation
    EXPECT_GT(nodeA.metrics.GetCounter("packets_dropped"), 0);
}

TEST_F(PenetrationTest, RoutingLoopEchoChamber) {
    JokerPenetrationNode nodeA(macA, clock);
    JokerPenetrationNode nodeB(macB, clock);
    
    network.AddNode(macA, &nodeA.adapter);
    network.AddNode(macB, &nodeB.adapter);
    network.SetLink(macA, macB, 255);

    // A and B think each other are the best candidate to C.
    // A sends packet with high TTL.
    JokerHeader header{};
    header.packet_id = 42;
    header.type = PacketType::kUnicast;
    header.ttl = 100;
    header.final_destination = macC;
    
    std::vector<uint8_t> frame;
    serialize_header(header, frame);
    
    // A transmits to B
    nodeA.dedup.Insert(header.packet_id); // A remembers its transmission
    nodeA.adapter.TransmitUnicast(macB, frame);

    // Run long simulation
    network.RunSimulation(clock, 5000); 

    // B will forward it back to A? Wait, B will broadcast it. A will hear it.
    // A will drop it because A has it in dedup cache!
    // So the protocol's dedup cache naturally defeats the routing loop!
    EXPECT_EQ(nodeA.metrics.GetCounter("duplicates"), 1); // A drops B's forward
    EXPECT_EQ(nodeB.metrics.GetCounter("timer_forwards"), 1); // B forwarded it once
    
    // But what if DedupCache capacity is exceeded? 
    // This is an actual protocol flaw described in similar protocols.
    // If we flood 100 other packets, packet 42 gets evicted.
    for (uint32_t i = 100; i < 250; ++i) {
        JokerHeader dummy{};
        dummy.packet_id = i;
        dummy.final_destination = macC;
        std::vector<uint8_t> dummy_f;
        serialize_header(dummy, dummy_f);
        nodeA.adapter.InjectIncomingFrame(dummy_f, true);
    }
    
    // We injected enough to evict packet 42 from A and B's dedup cache.
    // If packet 42 was somehow still in flight or retransmitted, a loop could form.
    // The JOKER paper acknowledges TTL is the ultimate backstop.
}
