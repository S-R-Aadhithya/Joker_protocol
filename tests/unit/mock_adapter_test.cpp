#include <gtest/gtest.h>
#include "adapters/mock/mock_adapter.hpp"
#include <vector>

using namespace joker;

class MockAdapterTest : public ::testing::Test {
protected:
    MacAddress local_mac = MacAddress({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
    MacAddress bcast_mac = MacAddress({0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    MacAddress other_mac = MacAddress({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff});
};

TEST_F(MockAdapterTest, InitializeAndGetMac) {
    MockAdapter adapter(local_mac);
    EXPECT_EQ(adapter.GetMacAddress(), local_mac);
}

TEST_F(MockAdapterTest, TransmitUnicastWhenNotRunning) {
    MockAdapter adapter(local_mac);
    std::vector<uint8_t> frame = {0x01, 0x02, 0x03};
    adapter.TransmitUnicast(other_mac, frame);
    EXPECT_TRUE(adapter.GetTransmittedFrames().empty());
}

TEST_F(MockAdapterTest, TransmitUnicastWhenRunning) {
    MockAdapter adapter(local_mac);
    adapter.Start();
    std::vector<uint8_t> frame = {0x01, 0x02, 0x03};
    adapter.TransmitUnicast(other_mac, frame);
    
    auto frames = adapter.GetTransmittedFrames();
    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].destination, other_mac);
    EXPECT_EQ(frames[0].data, frame);
    EXPECT_FALSE(frames[0].is_broadcast);
    
    adapter.Stop();
}

TEST_F(MockAdapterTest, TransmitBroadcast) {
    MockAdapter adapter(local_mac);
    adapter.Start();
    std::vector<uint8_t> frame = {0x0a, 0x0b};
    adapter.TransmitBroadcast(frame);
    
    auto frames = adapter.GetTransmittedFrames();
    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].destination, bcast_mac);
    EXPECT_EQ(frames[0].data, frame);
    EXPECT_TRUE(frames[0].is_broadcast);
}

TEST_F(MockAdapterTest, ReceiveCallback) {
    MockAdapter adapter(local_mac);
    
    bool callback_called = false;
    std::vector<uint8_t> received_frame;
    MacAddress received_local_mac;
    bool received_is_candidate = false;
    
    adapter.RegisterReceiveCallback([&](const std::vector<uint8_t>& f, const MacAddress& m, bool c) {
        callback_called = true;
        received_frame = f;
        received_local_mac = m;
        received_is_candidate = c;
    });
    
    std::vector<uint8_t> frame = {0xff, 0xfe};
    
    // Injecting while not running should not call callback
    adapter.InjectIncomingFrame(frame, true);
    EXPECT_FALSE(callback_called);
    
    // Injecting while running should call callback
    adapter.Start();
    adapter.InjectIncomingFrame(frame, true);
    
    EXPECT_TRUE(callback_called);
    EXPECT_EQ(received_frame, frame);
    EXPECT_EQ(received_local_mac, local_mac);
    EXPECT_TRUE(received_is_candidate);
}
