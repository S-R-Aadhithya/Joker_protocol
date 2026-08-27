#pragma once
#include <vector>
#include "joker/mac_address.hpp"
#include "joker/neighbor.hpp"
#include "joker/config.hpp"

namespace joker {

struct Candidate {
    MacAddress mac;
    double     lq;
    uint8_t    priority;   // 1 = highest [PAPER]
};

// [PAPER]/[DERIVED] — see ARCHITECTURE.md §9 for full algorithm narrative.
// Deterministic: given the same neighbor_table snapshot, always produces the
// same ordered candidate list (ties broken by ascending MAC — [ENGINEERING]).
std::vector<Candidate> select_candidates(
    const MacAddress& destination,
    const NeighborTable& neighbor_table,
    uint8_t candidate_count,
    const Config& config);

}  // namespace joker
