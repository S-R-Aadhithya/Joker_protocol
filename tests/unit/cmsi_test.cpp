#include <gtest/gtest.h>
#include "joker/cmsi.hpp"

using namespace joker;

TEST(CmsiTest, CalculateCmsiZero) {
    // [PAPER] At TP=0, CMSI should be 1.5
    EXPECT_DOUBLE_EQ(calculate_cmsi(0.0), 1.5);
}

TEST(CmsiTest, CalculateCmsiPositive) {
    // 0.006 * 1000 + 1.5 = 6.0 + 1.5 = 7.5
    EXPECT_DOUBLE_EQ(calculate_cmsi(1000.0), 7.5);
}

TEST(CmsiTest, SchedulerClampsMin) {
    SimpleTimerWheel wheel;
    CmsiScheduler scheduler(wheel, 2.0, 10.0);
    
    bool fired = false;
    // TP=0 -> calc is 1.5, min is 2.0 -> should clamp to 2.0 (2000ms)
    scheduler.Reschedule(0.0, [&](){ fired = true; });
    
    wheel.Tick(); // t=0, shouldn't fire
    EXPECT_FALSE(fired);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1950));
    wheel.Tick(); // t=1.95s, shouldn't fire
    EXPECT_FALSE(fired);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    wheel.Tick(); // t=2.05s, should fire
    EXPECT_TRUE(fired);
}

TEST(CmsiTest, SchedulerClampsMax) {
    SimpleTimerWheel wheel;
    CmsiScheduler scheduler(wheel, 1.0, 3.0);
    
    bool fired = false;
    // TP=1000 -> calc is 7.5, max is 3.0 -> should clamp to 3.0 (3000ms)
    scheduler.Reschedule(1000.0, [&](){ fired = true; });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(2900));
    wheel.Tick(); 
    EXPECT_FALSE(fired);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    wheel.Tick(); 
    EXPECT_TRUE(fired);
}
