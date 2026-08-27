#include <gtest/gtest.h>
#include "joker/metrics.hpp"

using namespace joker;

TEST(MetricsTest, IncrementCounter) {
    SimpleMetrics metrics;
    EXPECT_EQ(metrics.GetCounter("my_counter"), 0);

    metrics.Increment("my_counter");
    EXPECT_EQ(metrics.GetCounter("my_counter"), 1);

    metrics.Increment("my_counter", 5);
    EXPECT_EQ(metrics.GetCounter("my_counter"), 6);
}

TEST(MetricsTest, SetGauge) {
    SimpleMetrics metrics;
    EXPECT_DOUBLE_EQ(metrics.GetGauge("my_gauge"), 0.0);

    metrics.SetGauge("my_gauge", 42.5);
    EXPECT_DOUBLE_EQ(metrics.GetGauge("my_gauge"), 42.5);
}

TEST(MetricsTest, ObserveHistogram) {
    SimpleMetrics metrics;
    // We don't have GetHistogram, but we can verify it doesn't crash
    metrics.ObserveHistogram("my_hist", 10.5);
    metrics.ObserveHistogram("my_hist", 20.0);
}
