#pragma once
#include "joker/timer_wheel.hpp"
#include <map>

namespace joker {

class VirtualTimerWheel : public TimerWheel {
public:
    VirtualTimerWheel() : now_ms_(0), next_id_(1) {}

    TimerId Schedule(std::chrono::milliseconds delay, std::function<void()> callback) override {
        TimerId id = next_id_++;
        uint64_t expires_at = now_ms_ + delay.count();
        timers_.insert({expires_at, {id, std::move(callback)}});
        return id;
    }

    void Cancel(TimerId id) override {
        for (auto it = timers_.begin(); it != timers_.end(); ++it) {
            if (it->second.id == id) {
                it->second.cancelled = true;
                return;
            }
        }
    }

    void Tick() override {
        // Fire all timers that are <= now_ms_
        auto it = timers_.begin();
        while (it != timers_.end() && it->first <= now_ms_) {
            auto timer = std::move(it->second);
            it = timers_.erase(it);
            if (!timer.cancelled && timer.callback) {
                timer.callback();
            }
        }
    }

    // Advance virtual time by exact milliseconds
    void Advance(uint64_t ms) {
        // Step forward in 1ms increments to ensure we don't overshoot timers
        // and evaluate them precisely.
        for (uint64_t i = 0; i < ms; ++i) {
            now_ms_++;
            Tick();
        }
    }
    
    uint64_t Now() const { return now_ms_; }

private:
    struct Entry {
        TimerId id;
        std::function<void()> callback;
        bool cancelled = false;
    };
    uint64_t now_ms_;
    TimerId next_id_;
    std::multimap<uint64_t, Entry> timers_;
};

} // namespace joker
