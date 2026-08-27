#pragma once
#include <vector>
#include <memory>
#include <map>
#include "joker/mac_address.hpp"
#include "adapters/mock/mock_adapter.hpp"
#include "virtual_timer_wheel.hpp"

namespace joker {

class MockNetwork {
public:
    void AddNode(MacAddress mac, MockAdapter* adapter) {
        nodes_[mac] = adapter;
    }

    // Set symmetric link quality (1-255). 0 means out of range.
    void SetLink(MacAddress a, MacAddress b, uint8_t quality) {
        links_[{a, b}] = quality;
        links_[{b, a}] = quality;
    }

    void DispatchFrame(MacAddress sender, const std::vector<uint8_t>& frame, MacAddress link_target) {
        // Find all nodes in range of sender
        for (auto& pair : nodes_) {
            MacAddress receiver = pair.first;
            if (receiver == sender) continue;
            
            if (links_[{sender, receiver}] > 0) {
                bool is_candidate = (receiver == link_target) || link_target.IsBroadcast();
                pair.second->InjectIncomingFrame(frame, is_candidate);
            }
        }
    }

    // Advance time and propagate frames
    void RunSimulation(VirtualTimerWheel& clock, uint64_t duration_ms) {
        for (uint64_t i = 0; i < duration_ms; ++i) {
            clock.Advance(1); // advances by 1ms and calls tick
            
            // After tick, nodes might have transmitted. 
            for (auto& pair : nodes_) {
                auto* adapter = pair.second;
                auto tx_frames = adapter->GetTransmittedFrames();
                adapter->ClearTransmittedFrames();
                
                for (const auto& tx : tx_frames) {
                    DispatchFrame(adapter->GetMacAddress(), tx.data, tx.destination);
                }
            }
        }
    }

private:
    std::map<MacAddress, MockAdapter*> nodes_;
    std::map<std::pair<MacAddress, MacAddress>, uint8_t> links_;
};

} // namespace joker
