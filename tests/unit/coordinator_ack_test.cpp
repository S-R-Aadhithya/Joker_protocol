#include <gtest/gtest.h>
#include "joker/coordinator.hpp"
#include "joker/interface.hpp"
#include "joker/config.hpp"
#include "joker/metrics.hpp"
#include "adapters/mock/mock_adapter.hpp"



using namespace joker;

TEST(AckCoordinatorTest, CandidateSendsAckOnData) {
    AckCoordinator coord;
    JokerHeader header{};
    header.packet_id = 42;
    std::vector<uint8_t> frame = {0x01, 0x02};
    MacAddress local_mac = MacAddress::Parse("00:11:22:33:44:55").value();
    NeighborTable neighbors;
    MockAdapter nic(local_mac);
    SimpleMetrics metrics;
    Config config;

    coord.OnCandidateReceivedDataPacket(header, frame, 0, local_mac, neighbors, nic, metrics, config);

    EXPECT_EQ(metrics.GetCounter("ack_sent"), 1);
}

TEST(AckCoordinatorTest, TxAcceptsFirstAck) {
    AckCoordinator coord;
    auto now = std::chrono::steady_clock::now();
    coord.AddPending(42, {0x01}, now + std::chrono::milliseconds(50));
    EXPECT_EQ(coord.GetPendingCount(), 1);

    JokerHeader ack_header{};
    ack_header.packet_id = 42;
    ack_header.type = PacketType::kAck;
    ack_header.final_destination = MacAddress::Parse("00:11:22:33:44:55").value();

    coord.HandleAck(ack_header, {}, 0);

    // Pending should be erased once the first ACK is processed
    EXPECT_EQ(coord.GetPendingCount(), 0);
}

TEST(AckCoordinatorTest, ProcessTimeouts) {
    AckCoordinator coord;
    auto now = std::chrono::steady_clock::now();
    coord.AddPending(42, {0x01}, now + std::chrono::milliseconds(50));
    EXPECT_EQ(coord.GetPendingCount(), 1);

    // Should not erase before deadline
    coord.ProcessTimeouts(now + std::chrono::milliseconds(20));
    EXPECT_EQ(coord.GetPendingCount(), 1);

    // Should erase on or after deadline
    coord.ProcessTimeouts(now + std::chrono::milliseconds(51));
    EXPECT_EQ(coord.GetPendingCount(), 0);
}
