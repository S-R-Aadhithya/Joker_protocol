#pragma once
#include <vector>
#include <cstdint>
#include "joker/header.hpp"
#include "joker/neighbor.hpp"
#include "joker/dedup_cache.hpp"
#include "joker/config.hpp"
#include "joker/coordinator.hpp"
#include "joker/interface.hpp"
#include "joker/metrics.hpp"

namespace joker {

// process_received_frame(): the central RX decision pipeline.
// Called by the adapter for every frame delivered by the NIC (promiscuous
// mode means this includes frames not addressed to this node's MAC).
void process_received_frame(
    const std::vector<uint8_t>& raw_frame,
    const MacAddress& local_mac,
    bool is_candidate,
    NeighborTable& neighbors,
    DedupCache& dedup,
    Coordinator& coordinator,
    NicAdapter& nic,
    Metrics& metrics,
    const Config& config);

// [PAPER] Recognizing and accepting a lucky long transmission.
bool is_lucky_long_or_direct_delivery(const JokerHeader& header,
                                      const MacAddress& local_mac);

}  // namespace joker
