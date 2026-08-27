#include "joker/metrics.hpp"

namespace joker {

void SimpleMetrics::Increment(const std::string& name, uint64_t by) {
    std::lock_guard<std::mutex> lock(mu_);
    counters_[name] += by;
}

void SimpleMetrics::SetGauge(const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(mu_);
    gauges_[name] = value;
}

void SimpleMetrics::ObserveHistogram(const std::string& name, double value_ms) {
    std::lock_guard<std::mutex> lock(mu_);
    hist_sum_[name] += value_ms;
    hist_count_[name]++;
}

uint64_t SimpleMetrics::GetCounter(const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = counters_.find(name);
    return it != counters_.end() ? it->second : 0;
}

double SimpleMetrics::GetGauge(const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = gauges_.find(name);
    return it != gauges_.end() ? it->second : 0.0;
}

} // namespace joker
