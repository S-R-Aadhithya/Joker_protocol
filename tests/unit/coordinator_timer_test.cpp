#include <gtest/gtest.h>
#include "joker/coordinator.hpp"
#include "joker/timer_wheel.hpp"
#include "joker/interface.hpp" // For NicAdapter if we have it, else we mock
#include "joker/config.hpp"
#include "joker/metrics.hpp"

#include "adapters/mock/mock_adapter.hpp"



using namespace joker;

TEST(TimerCoordinatorTest, ScheduleAndFire) {
    SimpleTimerWheel wheel;
    TimerCoordinator coord(wheel);
    
    JokerHeader header{};
    header.packet_id = 100;
    std::vector<uint8_t> frame = {0x01, 0x02};
    MacAddress local_mac;
    NeighborTable neighbors;
    MockAdapter nic(MacAddress::Parse("00:11:22:33:44:55").value());
    SimpleMetrics metrics;
    Config config;
    config.twait_ms = 50;
    
    // Test receiving a packet (schedules a timer)
    coord.OnCandidateReceivedDataPacket(header, frame, 0, local_mac, neighbors, nic, metrics, config);
    
    EXPECT_EQ(coord.GetPendingCount(), 1);
    
    // Wait and tick
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    wheel.Tick();
    
    // Should have fired, incremented forwards, and cleared pending
    EXPECT_EQ(metrics.GetCounter("timer_forwards"), 1);
    EXPECT_EQ(coord.GetPendingCount(), 0);
}

TEST(TimerCoordinatorTest, OverheardSuppression) {
    SimpleTimerWheel wheel;
    TimerCoordinator coord(wheel);
    
    JokerHeader header{};
    header.packet_id = 101;
    std::vector<uint8_t> frame = {0x01, 0x02};
    MacAddress local_mac;
    NeighborTable neighbors;
    MockAdapter nic(MacAddress::Parse("00:11:22:33:44:55").value());
    SimpleMetrics metrics;
    Config config;
    config.twait_ms = 50;
    
    // Schedule
    coord.OnCandidateReceivedDataPacket(header, frame, 0, local_mac, neighbors, nic, metrics, config);
    EXPECT_EQ(coord.GetPendingCount(), 1);
    
    // Overhear forward before expiry
    coord.HandleOverheardForward(101);
    
    // Should be removed from pending
    EXPECT_EQ(coord.GetPendingCount(), 0);
    
    // Wait and tick
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    wheel.Tick();
    
    // Should NOT have fired
    EXPECT_EQ(metrics.GetCounter("timer_forwards"), 0);
}
