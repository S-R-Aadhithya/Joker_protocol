#pragma once
#include <functional>
#include <algorithm>
#include "joker/timer_wheel.hpp"

namespace joker {

// [PAPER] Eq. (3): CMSI = 0.006 * TP + 1.5, TP in kbps.
double calculate_cmsi(double throughput_kbps);

class CmsiScheduler {
public:
    CmsiScheduler(TimerWheel& wheel, double min_s, double max_s)
        : wheel_(wheel), min_s_(min_s), max_s_(max_s) {}

    // Called once at startup and again from each OGM-broadcast callback to
    // reschedule based on the freshest throughput measurement.
    void Reschedule(double throughput_kbps, std::function<void()> on_fire);

private:
    TimerWheel& wheel_;
    double min_s_;
    double max_s_;   // [ENGINEERING] safety clamp, not paper-specified
};

}  // namespace joker
