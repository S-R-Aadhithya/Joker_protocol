#pragma once
#include <vector>
#include <cstdint>
#include <functional>
#include "joker/mac_address.hpp"

namespace joker {

// Abstract Network Interface Controller (NIC) adapter.
// Represents a platform-specific mechanism for sending and receiving raw
// link-layer frames (e.g., Linux AF_PACKET, or a Mock).
class NicAdapter {
public:
    virtual ~NicAdapter() = default;

    // Transmit a unicast frame to the specified link-layer destination.
    virtual void TransmitUnicast(const MacAddress& destination, const std::vector<uint8_t>& frame) = 0;

    // Transmit a broadcast frame (e.g., an OGM) to the link-layer broadcast address.
    virtual void TransmitBroadcast(const std::vector<uint8_t>& frame) = 0;

    // Type of the callback invoked when a frame is received by the NIC.
    // The arguments are:
    // 1. The raw frame data (starting from the JOKER header, assuming the adapter
    //    strips the link-layer header, or provides a slice).
    // 2. The local MAC address of this NIC.
    // 3. A boolean indicating if this local MAC was the intended link-layer candidate
    //    (i.e. if the link-layer destination MAC matches the local MAC).
    using ReceiveCallback = std::function<void(const std::vector<uint8_t>& /* frame */,
                                               const MacAddress& /* local_mac */,
                                               bool /* is_candidate */)>;

    // Register the callback to be called on packet reception.
    virtual void RegisterReceiveCallback(ReceiveCallback callback) = 0;

    // Start listening for incoming frames. This may block depending on the implementation.
    virtual void Start() = 0;
    
    // Stop listening for incoming frames.
    virtual void Stop() = 0;

    // Get the MAC address of this adapter.
    [[nodiscard]] virtual MacAddress GetMacAddress() const = 0;
};

}  // namespace joker
