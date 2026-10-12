#include "joker/coordinator.hpp"
#include "joker/interface.hpp"
#include "joker/metrics.hpp"

namespace joker {

void handle_forward_timer_expiry(uint32_t packet_id, NicAdapter& nic,
                                 Metrics& metrics) {
    // Look up pending_[packet_id].frame, TTL-decrement (see §TTL below),
    // re-serialize destination as the next hop's top candidate, and
    // transmit. Remove from pending_ afterward.
    // nic.TransmitUnicast(next_hop, frame);
    metrics.Increment("timer_forwards");
}

void start_forward_timer(TimerWheel& wheel,
                         std::unordered_map<uint32_t, TimerCoordinator::PendingForward>& pending,
                         const JokerHeader& header,
                         const std::vector<uint8_t>& frame,
                         uint8_t local_priority,     // this candidate's own
                                                     // priority in the list
                         uint32_t twait_ms,
                         std::function<void()> on_expiry) {
    // [PAPER] wait = twait * (priority - 1)
    auto delay = std::chrono::milliseconds(
        static_cast<uint32_t>(twait_ms) * (local_priority - 1));

    TimerId id = wheel.Schedule(delay, std::move(on_expiry));
    pending[header.packet_id] = {id, frame};
}

void TimerCoordinator::OnCandidateReceivedDataPacket(
    const JokerHeader& header, const std::vector<uint8_t>& frame,
    size_t /*header_offset*/, const MacAddress& local_mac,
    NeighborTable& /*neighbors*/, NicAdapter& nic, Metrics& metrics,
    const Config& config) {

    uint8_t local_priority = 1; // Default to primary candidate
    for (size_t i = 0; i < header.other_candidates.size(); ++i) {
        if (header.other_candidates[i] == local_mac) {
            local_priority = static_cast<uint8_t>(2 + i);
            break;
        }
    }

    std::lock_guard<std::mutex> lock(pending_mutex_);
    start_forward_timer(wheel_, pending_, header, frame, local_priority,
        config.twait_ms,
        [this, packet_id = header.packet_id, &nic, &metrics]() {
            std::vector<uint8_t> frame_to_send;
            {
                std::lock_guard<std::mutex> inner_lock(this->pending_mutex_);
                auto it = this->pending_.find(packet_id);
                if (it != this->pending_.end()) {
                    frame_to_send = std::move(it->second.frame);
                    this->pending_.erase(it);
                }
            }
            if (!frame_to_send.empty()) {
                size_t consumed = 0;
                auto hdr = deserialize_header(frame_to_send, consumed);
                if (hdr && hdr->ttl > 1) {
                    hdr->ttl -= 1;
                    std::vector<uint8_t> new_frame;
                    serialize_header(*hdr, new_frame);
                    new_frame.insert(new_frame.end(), frame_to_send.begin() + consumed, frame_to_send.end());
                    nic.TransmitBroadcast(new_frame);
                } else if (hdr && hdr->ttl <= 1) {
                    metrics.Increment("ttl_expired");
                    return;
                } else {
                    nic.TransmitBroadcast(frame_to_send);
                }
                handle_forward_timer_expiry(packet_id, nic, metrics);
            }
        });
}

void TimerCoordinator::HandleOverheardForward(uint32_t packet_id) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    auto it = pending_.find(packet_id);
    if (it == pending_.end()) return;   // not something we were waiting on
    wheel_.Cancel(it->second.timer_id);  // [PAPER] suppress own forwarding
    pending_.erase(it);
}

}  // namespace joker
