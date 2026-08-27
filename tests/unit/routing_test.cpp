#include <gtest/gtest.h>
#include "joker/routing.hpp"

using namespace joker;

TEST(RoutingTest, FadeMargin) {
    EXPECT_DOUBLE_EQ(calculate_fade_margin(-60.0, -74.0), 14.0);
    EXPECT_DOUBLE_EQ(calculate_fade_margin(-80.0, -74.0), -6.0);
}

TEST(RoutingTest, DistancePenalty) {
    EXPECT_EQ(distance_penalty(9.9), 1);
    EXPECT_EQ(distance_penalty(10.0), 3);
    EXPECT_EQ(distance_penalty(15.0), 3);
    EXPECT_EQ(distance_penalty(20.0), 3);
    EXPECT_EQ(distance_penalty(20.1), 5);
}

TEST(RoutingTest, JokerLQ) {
    // LQ = TQ * (TQmax - Distance_penalty) / TQmax
    // Example: TQ = 200, DP = 3, TQmax = 255
    // LQ = 200 * (255 - 3) / 255 = 200 * 252 / 255 = 197.647...
    double lq = calculate_joker_lq(200, 3, 255);
    EXPECT_NEAR(lq, 197.647, 0.001);

    // Div by zero guard
    EXPECT_DOUBLE_EQ(calculate_joker_lq(100, 3, 0), 0.0);

    // Clamp boundary
    EXPECT_DOUBLE_EQ(calculate_joker_lq(255, 1, 255), 254.0);
}

TEST(RoutingTest, CalculateTq) {
    uint16_t tq_max = 255;
    
    // Perfect link
    EXPECT_EQ(calculate_tq(255, 255, 1.0, 1.0, tq_max), 255);
    
    // Bad local reception
    EXPECT_EQ(calculate_tq(127, 255, 1.0, 1.0, tq_max), 127);
    
    // Bad reported reception
    EXPECT_EQ(calculate_tq(255, 127, 1.0, 1.0, tq_max), 127);
    
    // Bad in both directions
    // (127/255) * (127/255) * 255 = 0.498 * 0.498 * 255 = ~63
    EXPECT_EQ(calculate_tq(127, 127, 1.0, 1.0, tq_max), 63);
    
    // Asymmetric penalty
    EXPECT_EQ(calculate_tq(255, 255, 0.5, 1.0, tq_max), 128); // 255 * 0.5 rounded = 128
    
    // Hop penalty
    EXPECT_EQ(calculate_tq(255, 255, 1.0, 0.9, tq_max), 230); // 255 * 0.9 = 229.5 -> 230
}
