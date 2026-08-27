#include "joker/coordinator.hpp"
#include "joker/interface.hpp"
#include "joker/metrics.hpp"

namespace joker {

// Candidate side: receiving a DATA packet under ACK-based coordination.
void AckCoordinator::OnCandidateReceivedDataPacket(
    const JokerHeader& header, const std::vector<uint8_t>& frame,
    size_t /*header_offset*/, const MacAddress& local_mac,
    NeighborTable& /*neighbors*/, NicAdapter& nic, Metrics& metrics,
    const Config& config) {

    // [PAPER] "once the packet is received, the ACK message is generated and sent"
    JokerHeader ack{};
    ack.type = PacketType::kAck;
    ack.ttl = 1;
    ack.packet_id = header.packet_id;
    ack.final_destination = local_mac;

    std::vector<uint8_t> wire;
    serialize_header(ack, wire);
    nic.TransmitUnicast(MacAddress{}, wire); // mock: next hop MAC
    metrics.Increment("ack_sent");
}

// TX side: correlate an incoming ACK with a pending coordination.
void AckCoordinator::HandleAck(const JokerHeader& header,
                               const std::vector<uint8_t>& /*frame*/,
                               size_t /*offset*/) {
    auto it = pending_.find(header.packet_id);
    if (it == pending_.end()) return; // Stale/unknown ACK

    if (it->second.first_ack_sender.has_value()) {
        return; // Already resolved
    }

    it->second.first_ack_sender = header.final_destination;

    JokerHeader fwd{};
    fwd.type = PacketType::kForwarding;
    fwd.ttl = 1;
    fwd.packet_id = header.packet_id;
    fwd.final_destination = *it->second.first_ack_sender;
    std::vector<uint8_t> wire;
    serialize_header(fwd, wire);
    
    // In a real implementation we need nic.TransmitUnicast here.
    // For now we remove it from pending.
    pending_.erase(it);
}

// Candidate side: receiving the Forwarding message means THIS node was selected.
void AckCoordinator::HandleForwardingMessage(const JokerHeader& header,
                                             const std::vector<uint8_t>& /*frame*/,
                                             size_t /*offset*/) {
    // We would forward the retained frame here.
}

// [ENGINEERING] Process timeouts
void AckCoordinator::ProcessTimeouts(const std::chrono::steady_clock::time_point& now) {
    for (auto it = pending_.begin(); it != pending_.end(); ) {
        if (now >= it->second.deadline) {
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

// For tests: simulate the sender transmitting a DATA packet
// Since we don't have a Sender interface, we can populate pending manually for tests.
// Wait, I should add a helper to add to pending if needed, or define it in hpp.

void AckCoordinator::AddPending(uint32_t packet_id, const std::vector<uint8_t>& frame, const std::chrono::steady_clock::time_point& deadline) {
    pending_[packet_id] = {packet_id, frame, deadline, std::nullopt};
}

} // namespace joker
