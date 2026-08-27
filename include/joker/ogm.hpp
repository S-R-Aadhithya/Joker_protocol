#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include "joker/mac_address.hpp"
#include "joker/neighbor.hpp"

namespace joker {

// [ENGINEERING/DERIVED] wire format: the paper describes OGM's role and
// origin (BATMAN-inherited "tiny packet") but does not give a byte-level
// OGM layout. This struct is a documented, versioned engineering proposal,
// not a paper-mandated format. It carries the minimum fields JOKER's TQ/LQ
// pipeline needs, modeled on BATMAN's OGM fields.
struct Ogm {
    MacAddress originator;      // [DERIVED] node this OGM describes
    uint32_t   sequence_number; // [DERIVED] BATMAN-style, for freshness/loop avoidance
    uint8_t    tq_reported;     // [DERIVED] TQ as computed by the sender toward `originator`
    uint8_t    ttl;              // [DERIVED] bounds OGM flood radius, analogous to BATMAN OGM TTL
};

// Builds this node's own OGM (originator == self).
Ogm ogm_build(const MacAddress& self_mac, uint32_t next_sequence_number, uint8_t initial_ttl);

// Broadcasts an OGM via the NIC adapter (link-layer broadcast address).
// (commented out NicAdapter since we don't have it explicitly bound here yet)
// void ogm_send(const Ogm& ogm /*, NicAdapter& nic */);

// Called on receipt of a (possibly relayed) OGM frame.
std::optional<Ogm> ogm_receive(const std::vector<uint8_t>& raw_frame);

// Updates neighbor/routing state from a received OGM, and decides whether
// to selectively rebroadcast it.
void ogm_process(const Ogm& ogm, NeighborTable& neighbors, double rx_power_dbm);

}  // namespace joker
