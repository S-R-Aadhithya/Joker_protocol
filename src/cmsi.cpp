#include "joker/cmsi.hpp"

namespace joker {

double calculate_cmsi(double throughput_kbps) {
    return 0.006 * throughput_kbps + 1.5;   // [PAPER] Eq. 3
}

void CmsiScheduler::Reschedule(double throughput_kbps,
                                 std::function<void()> on_fire) {
    double interval_s = calculate_cmsi(throughput_kbps);
    interval_s = std::clamp(interval_s, min_s_, max_s_);   // [ENGINEERING]
    auto delay = std::chrono::milliseconds(
        static_cast<int64_t>(interval_s * 1000.0));
    wheel_.Schedule(delay, std::move(on_fire));
}

}  // namespace joker
