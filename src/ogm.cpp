#include "joker/ogm.hpp"
#include <chrono>

namespace joker {

Ogm ogm_build(const MacAddress& self_mac, uint32_t next_sequence_number, uint8_t initial_ttl) {
    Ogm ogm{};
    ogm.originator = self_mac;
    ogm.sequence_number = next_sequence_number;
    ogm.tq_reported = 255; // Sender (originator) always reports max TQ for itself
    ogm.ttl = initial_ttl;
    return ogm;
}

std::optional<Ogm> ogm_receive(const std::vector<uint8_t>& raw_frame) {
    // For now, this is a stub. Full wire format serialization for OGM 
    // would be implemented similarly to JokerHeader.
    return std::nullopt;
}

void ogm_process(const Ogm& ogm, NeighborTable& neighbors, double rx_power_dbm) {
    NeighborEntry entry;
    // We treat the OGM's originator as the immediate neighbor in this simplified model.
    // In a full BATMAN implementation, the originator and the immediate sender (neighbor) 
    // might be different (for relayed OGMs).
    entry.mac = ogm.originator;
    entry.last_seen = std::chrono::steady_clock::now();
    entry.tq_recv = ogm.tq_reported;
    entry.rx_power_dbm = rx_power_dbm;
    
    // Defaulting other values for now
    entry.tq_local = 255; 
    entry.f_asym = 1.0;
    entry.hop_penalty = 1.0;
    entry.reachable_to_dest = true;

    neighbors.Update(entry);
}

}  // namespace joker
