#pragma once
#include <string>
#include <unordered_map>
#include <mutex>

namespace joker {

class Metrics {
public:
    virtual ~Metrics() = default;
    virtual void Increment(const std::string& name, uint64_t by = 1) = 0;
    virtual void SetGauge(const std::string& name, double value) = 0;
    virtual void ObserveHistogram(const std::string& name, double value_ms) = 0;
};

class SimpleMetrics : public Metrics {
public:
    void Increment(const std::string& name, uint64_t by = 1) override;
    void SetGauge(const std::string& name, double value) override;
    void ObserveHistogram(const std::string& name, double value_ms) override;

    uint64_t GetCounter(const std::string& name);
    double GetGauge(const std::string& name);

private:
    std::mutex mu_;
    std::unordered_map<std::string, uint64_t> counters_;
    std::unordered_map<std::string, double> gauges_;
    // Simplified histogram: just store sum and count
    std::unordered_map<std::string, double> hist_sum_;
    std::unordered_map<std::string, uint64_t> hist_count_;
};

} // namespace joker
