#include "joker/timer_wheel.hpp"
#include <algorithm>

namespace joker {

TimerId SimpleTimerWheel::Schedule(std::chrono::milliseconds delay,
                                   std::function<void()> callback) {
    TimerId id = next_id_++;
    auto expires_at = std::chrono::steady_clock::now() + delay;
    
    timers_.push_back({id, expires_at, std::move(callback), false});
    
    // Maintain max-heap property (earliest expiry at the front/top of the heap conceptually,
    // so we use std::push_heap and std::pop_heap to keep the earliest at timers_.front())
    std::push_heap(timers_.begin(), timers_.end());
    
    return id;
}

void SimpleTimerWheel::Cancel(TimerId id) {
    // Find the timer and mark it as cancelled
    for (auto& entry : timers_) {
        if (entry.id == id) {
            entry.cancelled = true;
            break;
        }
    }
}

void SimpleTimerWheel::Tick() {
    auto now = std::chrono::steady_clock::now();
    
    // Process all expired timers
    while (!timers_.empty() && timers_.front().expires_at <= now) {
        // Pop the earliest timer
        std::pop_heap(timers_.begin(), timers_.end());
        TimerEntry entry = std::move(timers_.back());
        timers_.pop_back();
        
        // Execute if not cancelled
        if (!entry.cancelled && entry.callback) {
            entry.callback();
        }
    }
}

}  // namespace joker
