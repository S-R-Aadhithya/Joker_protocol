#pragma once
#include <vector>
#include <cstdint>
#include <mutex>
#include <queue>
#include "joker/interface.hpp"
#include "joker/mac_address.hpp"

namespace joker {

// A Mock implementation of NicAdapter for testing and simulation.
class MockAdapter : public NicAdapter {
public:
    explicit MockAdapter(MacAddress mac) : mac_(mac), running_(false) {}

    void TransmitUnicast(const MacAddress& destination, const std::vector<uint8_t>& frame) override;
    void TransmitBroadcast(const std::vector<uint8_t>& frame) override;

    void RegisterReceiveCallback(ReceiveCallback callback) override;

    void Start() override;
    void Stop() override;

    [[nodiscard]] MacAddress GetMacAddress() const override { return mac_; }

    // --- Mock-specific injection/inspection APIs ---

    // Simulates the NIC receiving a frame from the air.
    // This will trigger the registered callback if the adapter is started.
    void InjectIncomingFrame(const std::vector<uint8_t>& frame, bool is_candidate);

    struct TransmittedFrame {
        MacAddress destination; // Broadcast if IsBroadcast is true
        std::vector<uint8_t> data;
        bool is_broadcast;
    };

    // Inspect the list of frames transmitted by this adapter.
    std::vector<TransmittedFrame> GetTransmittedFrames() const;

    // Clear the list of transmitted frames.
    void ClearTransmittedFrames();

private:
    MacAddress mac_;
    bool running_;
    ReceiveCallback callback_;

    mutable std::mutex tx_mutex_;
    std::vector<TransmittedFrame> transmitted_frames_;
};

}  // namespace joker
