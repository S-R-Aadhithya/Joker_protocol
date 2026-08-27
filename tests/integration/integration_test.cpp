#include <gtest/gtest.h>
#include "joker/forwarding.hpp"
#include "joker/coordinator.hpp"
#include "joker/metrics.hpp"
#include "joker/config.hpp"
#include "mock_network.hpp"
#include "joker/timer_wheel.hpp"
#include "virtual_timer_wheel.hpp"

using namespace joker;

struct JokerNode {
    MacAddress mac;
    MockAdapter adapter;
    NeighborTable neighbors;
    DedupCache dedup;
    TimerCoordinator coordinator;
    SimpleMetrics metrics;
    Config config;

    JokerNode(MacAddress mac, VirtualTimerWheel& wheel) 
        : mac(mac), adapter(mac), dedup(100, std::chrono::milliseconds(5000)), coordinator(wheel) {
        
        config.twait_ms = 50;
        config.candidate_count = 3;
        
        adapter.RegisterReceiveCallback([this](const std::vector<uint8_t>& frame, const MacAddress&, bool is_candidate) {
            process_received_frame(frame, this->mac, is_candidate, this->neighbors, this->dedup, this->coordinator, this->adapter, this->metrics, this->config);
        });
        adapter.Start();
    }
};

class IntegrationTest : public ::testing::Test {
protected:
    VirtualTimerWheel clock;
    MockNetwork network;

    MacAddress macA = MacAddress::Parse("00:00:00:00:00:01").value();
    MacAddress macB = MacAddress::Parse("00:00:00:00:00:02").value();
    MacAddress macC = MacAddress::Parse("00:00:00:00:00:03").value();
    MacAddress macD = MacAddress::Parse("00:00:00:00:00:04").value();
};

TEST_F(IntegrationTest, TimerFlowAndSuppression) {
    JokerNode nodeA(macA, clock);
    JokerNode nodeB(macB, clock);
    JokerNode nodeC(macC, clock);

    network.AddNode(macA, &nodeA.adapter);
    network.AddNode(macB, &nodeB.adapter);
    network.AddNode(macC, &nodeC.adapter);

    network.SetLink(macA, macB, 255);
    network.SetLink(macA, macC, 255);
    network.SetLink(macB, macC, 255);

    // A wants to send to D (unknown). B is primary candidate, C is secondary.
    JokerHeader header{};
    header.packet_id = 100;
    header.type = PacketType::kUnicast;
    header.ttl = 32;
    header.final_destination = macD;
    header.other_candidates.push_back(macC); 

    std::vector<uint8_t> frame;
    serialize_header(header, frame);

    // A transmits to B (primary)
    nodeA.adapter.TransmitUnicast(macB, frame);

    // B (priority 0) wait time = 50 / 2 = 25ms
    // C (priority 1) wait time = 50
    network.RunSimulation(clock, 30); // Advance 30ms

    // B should have forwarded it. C should have suppressed its timer because it overheard B.
    EXPECT_EQ(nodeB.metrics.GetCounter("timer_forwards"), 1);
    
    // C overhears B's forward.
    // C's timer is suppressed, so even if we wait 30 more ms, it won't forward.
    network.RunSimulation(clock, 30);
    EXPECT_EQ(nodeC.metrics.GetCounter("timer_forwards"), 0);
    EXPECT_EQ(nodeC.metrics.GetCounter("overheard_packets_not_candidate"), 1); // C overheard B's forward to D
}

TEST_F(IntegrationTest, LuckyLongTransmission) {
    JokerNode nodeA(macA, clock);
    JokerNode nodeB(macB, clock);
    JokerNode nodeC(macC, clock);

    network.AddNode(macA, &nodeA.adapter);
    network.AddNode(macB, &nodeB.adapter);
    network.AddNode(macC, &nodeC.adapter);

    // Topology: A -> B -> C. But A has weak link directly to C!
    network.SetLink(macA, macB, 255);
    network.SetLink(macB, macC, 255);
    network.SetLink(macA, macC, 1); // weak link

    // A sends a packet destined for C, but routes through B
    JokerHeader header{};
    header.packet_id = 200;
    header.type = PacketType::kUnicast;
    header.ttl = 32;
    header.final_destination = macC;

    std::vector<uint8_t> frame;
    serialize_header(header, frame);

    // A transmits to B
    nodeA.dedup.Insert(header.packet_id); // A remembers its own packet
    nodeA.adapter.TransmitUnicast(macB, frame);

    network.RunSimulation(clock, 5); 

    // C overheard the packet directly from A because of the weak link.
    // C is the final destination, so it accepts it immediately via Lucky Long Transmission!
    EXPECT_EQ(nodeC.metrics.GetCounter("packets_delivered_lucky_or_direct"), 1);
    
    // B also received it and will eventually forward it, but C will drop it as a duplicate
    network.RunSimulation(clock, 30); // B forwards
    EXPECT_EQ(nodeB.metrics.GetCounter("timer_forwards"), 1);
    
    network.RunSimulation(clock, 5); // C receives B's forward
    // C drops B's forward as a duplicate
    EXPECT_EQ(nodeC.metrics.GetCounter("duplicates"), 1);
    EXPECT_EQ(nodeC.metrics.GetCounter("packets_delivered_lucky_or_direct"), 1); // Not incremented again
}

TEST_F(IntegrationTest, TtlExpiration) {
    JokerNode nodeA(macA, clock);
    JokerNode nodeB(macB, clock);

    network.AddNode(macA, &nodeA.adapter);
    network.AddNode(macB, &nodeB.adapter);
    network.SetLink(macA, macB, 255);

    // A sends packet with TTL=0
    JokerHeader header{};
    header.packet_id = 300;
    header.type = PacketType::kUnicast;
    header.ttl = 0;
    header.final_destination = macC;

    std::vector<uint8_t> frame;
    serialize_header(header, frame);

    nodeA.adapter.TransmitUnicast(macB, frame);
    network.RunSimulation(clock, 5);

    // B drops the packet because TTL is expired
    EXPECT_EQ(nodeB.metrics.GetCounter("ttl_expired"), 1);
    EXPECT_EQ(nodeB.metrics.GetCounter("timer_forwards"), 0);
}
