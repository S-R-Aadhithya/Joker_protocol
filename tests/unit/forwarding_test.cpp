#include <gtest/gtest.h>
#include "joker/forwarding.hpp"
#include "joker/interface.hpp"
#include "joker/config.hpp"
#include "joker/metrics.hpp"
#include "joker/coordinator.hpp"
#include "adapters/mock/mock_adapter.hpp"
#include "joker/timer_wheel.hpp"
#include <map>



using namespace joker;

class ForwardingTest : public ::testing::Test {
protected:
    MacAddress local_mac = MacAddress::Parse("00:11:22:33:44:55").value();
    NeighborTable neighbors;
    DedupCache dedup{100, std::chrono::milliseconds(1000)};
    SimpleTimerWheel wheel;
    TimerCoordinator coordinator{wheel};
    MockAdapter nic{local_mac};
    SimpleMetrics metrics;
    Config config;
};

TEST_F(ForwardingTest, DropsMalformedHeader) {
    std::vector<uint8_t> bad_frame = {0x00, 0x01}; // Too short
    process_received_frame(bad_frame, local_mac, true, neighbors, dedup, coordinator, nic, metrics, config);
    EXPECT_EQ(metrics.GetCounter("packets_dropped"), 1);
    EXPECT_EQ(metrics.GetCounter("malformed_header"), 1);
}

TEST_F(ForwardingTest, LuckyLongTransmission) {
    JokerHeader header{};
    header.packet_id = 42;
    header.type = PacketType::kUnicast;
    header.ttl = 5;
    header.final_destination = local_mac;
    
    std::vector<uint8_t> frame;
    serialize_header(header, frame);

    // We are not a candidate, but it's addressed to us
    process_received_frame(frame, local_mac, false, neighbors, dedup, coordinator, nic, metrics, config);

    EXPECT_EQ(metrics.GetCounter("packets_delivered_lucky_or_direct"), 1);
    EXPECT_TRUE(dedup.Contains(42));
}

TEST_F(ForwardingTest, TtlZeroDropped) {
    JokerHeader header{};
    header.packet_id = 42;
    header.type = PacketType::kUnicast;
    header.ttl = 0;
    header.final_destination = MacAddress::Parse("aa:bb:cc:dd:ee:ff").value();
    
    std::vector<uint8_t> frame;
    serialize_header(header, frame);

    process_received_frame(frame, local_mac, true, neighbors, dedup, coordinator, nic, metrics, config);

    EXPECT_EQ(metrics.GetCounter("ttl_expired"), 1);
    EXPECT_EQ(metrics.GetCounter("packets_dropped"), 1);
}

TEST_F(ForwardingTest, DuplicateDropped) {
    JokerHeader header{};
    header.packet_id = 42;
    header.type = PacketType::kUnicast;
    header.ttl = 5;
    header.final_destination = MacAddress::Parse("aa:bb:cc:dd:ee:ff").value();
    
    std::vector<uint8_t> frame;
    serialize_header(header, frame);

    process_received_frame(frame, local_mac, true, neighbors, dedup, coordinator, nic, metrics, config);
    EXPECT_EQ(metrics.GetCounter("candidate_packets_received"), 1);

    // Send again
    process_received_frame(frame, local_mac, true, neighbors, dedup, coordinator, nic, metrics, config);
    EXPECT_EQ(metrics.GetCounter("duplicates"), 1);
}

TEST_F(ForwardingTest, OverheardNonCandidateSuppressesTimer) {
    JokerHeader header{};
    header.packet_id = 100;
    header.type = PacketType::kUnicast;
    header.ttl = 5;
    header.final_destination = MacAddress::Parse("aa:bb:cc:dd:ee:ff").value();
    
    std::vector<uint8_t> frame;
    serialize_header(header, frame);

    // Simulate we already received it as candidate and have it pending
    coordinator.pending_[100] = {TimerId{1}, frame};
    
    // Now we overhear it again, but not as a candidate (maybe from another candidate)
    process_received_frame(frame, local_mac, false, neighbors, dedup, coordinator, nic, metrics, config);

    EXPECT_EQ(metrics.GetCounter("overheard_packets_not_candidate"), 1);
    EXPECT_EQ(coordinator.GetPendingCount(), 0); // Should be suppressed
}
