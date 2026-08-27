#include <gtest/gtest.h>
#include "joker/timer_wheel.hpp"
#include <vector>
#include <thread>
#include <chrono>

using namespace joker;

TEST(TimerWheelTest, BasicSchedulingAndTick) {
    SimpleTimerWheel wheel;
    bool fired = false;

    // Schedule a timer for 10ms
    wheel.Schedule(std::chrono::milliseconds(10), [&fired]() {
        fired = true;
    });

    // Tick immediately; shouldn't fire
    wheel.Tick();
    EXPECT_FALSE(fired);

    // Wait and tick again
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    wheel.Tick();
    EXPECT_TRUE(fired);
}

TEST(TimerWheelTest, Cancellation) {
    SimpleTimerWheel wheel;
    bool fired = false;

    // Schedule a timer for 10ms
    TimerId id = wheel.Schedule(std::chrono::milliseconds(10), [&fired]() {
        fired = true;
    });

    // Cancel it
    wheel.Cancel(id);

    // Wait and tick
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    wheel.Tick();

    // Should not have fired
    EXPECT_FALSE(fired);
}

TEST(TimerWheelTest, MultipleTimersOrder) {
    SimpleTimerWheel wheel;
    std::vector<int> execution_order;

    // Schedule in reverse order of expiry
    wheel.Schedule(std::chrono::milliseconds(30), [&]() { execution_order.push_back(3); });
    wheel.Schedule(std::chrono::milliseconds(10), [&]() { execution_order.push_back(1); });
    wheel.Schedule(std::chrono::milliseconds(20), [&]() { execution_order.push_back(2); });

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    wheel.Tick();

    ASSERT_EQ(execution_order.size(), 3);
    EXPECT_EQ(execution_order[0], 1);
    EXPECT_EQ(execution_order[1], 2);
    EXPECT_EQ(execution_order[2], 3);
}
