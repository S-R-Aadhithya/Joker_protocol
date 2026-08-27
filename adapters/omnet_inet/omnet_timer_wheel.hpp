#pragma once
#include "joker/timer_wheel.hpp"
#include <omnetpp.h>
#include <map>

namespace joker {

// A TimerWheel implementation that integrates directly with OMNeT++'s 
// discrete event simulation engine.
class OmnetTimerWheel : public TimerWheel {
public:
    explicit OmnetTimerWheel(omnetpp::cSimpleModule* module) : module_(module) {}

    ~OmnetTimerWheel() override {
        for (auto& pair : active_timers_) {
            module_->cancelAndDelete(pair.second);
        }
    }

    TimerId Schedule(std::chrono::milliseconds delay, std::function<void()> callback) override {
        TimerId id = next_id_++;
        
        // Create an OMNeT++ self-message
        omnetpp::cMessage* msg = new omnetpp::cMessage("JokerTimer");
        
        // Store the callback in a custom context
        auto* cb_ptr = new std::function<void()>(std::move(callback));
        msg->setContextPointer(cb_ptr);

        // Schedule the message using OMNeT++'s simTime
        omnetpp::simtime_t sim_delay = omnetpp::SimTime(delay.count(), omnetpp::SIMTIME_MS);
        module_->scheduleAt(omnetpp::simTime() + sim_delay, msg);
        
        active_timers_[id] = msg;
        return id;
    }

    void Cancel(TimerId id) override {
        auto it = active_timers_.find(id);
        if (it != active_timers_.end()) {
            omnetpp::cMessage* msg = it->second;
            module_->cancelAndDelete(msg);
            
            // Cleanup context pointer
            auto* cb_ptr = static_cast<std::function<void()>*>(msg->getContextPointer());
            delete cb_ptr;
            
            active_timers_.erase(it);
        }
    }

    void Tick() override {
        // Not used in OMNeT++. Time advances via handleMessage().
    }

    // Must be called by the OMNeT module's handleMessage when a self-message arrives.
    void HandleTimerEvent(omnetpp::cMessage* msg) {
        auto* cb_ptr = static_cast<std::function<void()>*>(msg->getContextPointer());
        if (cb_ptr && *cb_ptr) {
            (*cb_ptr)();
        }
        delete cb_ptr;
        
        for (auto it = active_timers_.begin(); it != active_timers_.end(); ++it) {
            if (it->second == msg) {
                active_timers_.erase(it);
                break;
            }
        }
        delete msg;
    }

private:
    omnetpp::cSimpleModule* module_;
    TimerId next_id_ = 1;
    std::map<TimerId, omnetpp::cMessage*> active_timers_;
};

} // namespace joker
