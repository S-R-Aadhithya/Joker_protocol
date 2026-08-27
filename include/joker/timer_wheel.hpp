#pragma once
#include <cstdint>
#include <functional>
#include <chrono>

namespace joker {

using TimerId = uint64_t;

class TimerWheel {
public:
    virtual ~TimerWheel() = default;

    // Schedules `callback` to fire after `delay`; returns an id usable for
    // cancellation. Must be safe to cancel from within a callback (no
    // reentrancy deadlocks) and must not double-fire a cancelled timer that
    // races with expiry (ARCHITECTURE.md §15).
    virtual TimerId Schedule(std::chrono::milliseconds delay,
                              std::function<void()> callback) = 0;
    
    virtual void Cancel(TimerId id) = 0;
    
    virtual void Tick() = 0;   // called by the event loop
};

// A simple implementation of TimerWheel for the core logic
class SimpleTimerWheel : public TimerWheel {
public:
    TimerId Schedule(std::chrono::milliseconds delay,
                     std::function<void()> callback) override;
    void Cancel(TimerId id) override;
    void Tick() override;

private:
    struct TimerEntry {
        TimerId id;
        std::chrono::steady_clock::time_point expires_at;
        std::function<void()> callback;
        bool cancelled = false;

        bool operator<(const TimerEntry& other) const {
            // priority_queue returns the largest element first, so we reverse
            // the comparison to get the earliest expiry time at the top.
            return expires_at > other.expires_at;
        }
    };

    // Note: for a production implementation, a more efficient structure
    // like a true hashed timing wheel or a std::set might be used to allow
    // efficient O(1) or O(log N) cancellation. Here we use an unordered_map
    // to mark timers as cancelled by id, and a vector to hold the queue.
    TimerId next_id_ = 1;
    std::vector<TimerEntry> timers_;
};

}  // namespace joker
