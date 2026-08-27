#include "joker/forwarding.hpp"
#include "joker/candidate.hpp"
#include "joker/interface.hpp"
#include "joker/coordinator.hpp"
#include "joker/metrics.hpp"

namespace joker {

bool is_lucky_long_or_direct_delivery(const JokerHeader& header,
                                      const MacAddress& local_mac) {
    return header.final_destination == local_mac;
}

void process_received_frame(
    const std::vector<uint8_t>& raw_frame,
    const MacAddress& local_mac,
    bool is_candidate,
    NeighborTable& neighbors,
    DedupCache& dedup,
    Coordinator& coordinator,
    NicAdapter& nic,
    Metrics& metrics,
    const Config& config) {

    metrics.Increment("packets_received");

    // 1. Strip link-layer header, isolate JOKER payload region.
    size_t consumed = 0;
    auto header_opt = deserialize_header(raw_frame, consumed);
    if (!header_opt) {
        metrics.Increment("packets_dropped");
        metrics.Increment("malformed_header");
        return;
    }
    JokerHeader header = *header_opt;

    // 2. Structural validation.
    if (!validate_header(header, config.candidate_count)) {
        metrics.Increment("packets_dropped");
        metrics.Increment("invalid_header");
        return;
    }

    // 3. Packet-type dispatch.
    switch (header.type) {
        case PacketType::kAck:
            coordinator.HandleAck(header, raw_frame, consumed);
            return;
        case PacketType::kForwarding:
            coordinator.HandleForwardingMessage(header, raw_frame, consumed);
            return;
        case PacketType::kUnicast:
            break;  // fall through to data-packet handling below
    }

    // 4. Lucky long transmission check (Phase 10)
    if (is_lucky_long_or_direct_delivery(header, local_mac)) {
        if (dedup.Contains(header.packet_id)) {
            metrics.Increment("duplicates");
            return;
        }
        dedup.Insert(header.packet_id);
        metrics.Increment("packets_delivered_lucky_or_direct");
        // In a real system, we deliver to network layer here.
        return;
    }

    // 5. TTL check (Phase 11)
    if (header.ttl == 0) {
        metrics.Increment("ttl_expired");
        metrics.Increment("packets_dropped");
        return;
    }

    // 6. Duplicate suppression.
    if (dedup.Contains(header.packet_id)) {
        metrics.Increment("duplicates");
        return;
    }
    dedup.Insert(header.packet_id);

    // 7. Not destination, not lucky — check if candidate.
    if (!is_candidate) {
        metrics.Increment("overheard_packets_not_candidate");
        // Check if we need to suppress a timer for an overheard forward
        // If the coordinator is a TimerCoordinator, this is handled via cast
        // or a virtual method. We added it to TimerCoordinator specifically, but we could cast.
        if (auto* tc = dynamic_cast<TimerCoordinator*>(&coordinator)) {
             tc->HandleOverheardForward(header.packet_id);
        }
        // Future: Passively update Link Quality (LQ) metrics here
        return;
    }

    metrics.Increment("candidate_packets_received");

    // 8. Hand off to the configured coordination scheme.
    coordinator.OnCandidateReceivedDataPacket(header, raw_frame, consumed,
                                              local_mac, neighbors, nic, metrics,
                                              config);
}

}  // namespace joker
