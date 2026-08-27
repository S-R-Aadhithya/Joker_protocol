#include "joker/candidate.hpp"
#include "joker/routing.hpp"
#include <algorithm>
#include <chrono>

namespace joker {

std::vector<Candidate> select_candidates(
    const MacAddress& destination,
    const NeighborTable& neighbor_table,
    uint8_t candidate_count,
    const Config& config) {

    auto now = std::chrono::steady_clock::now();
    auto expiry = std::chrono::milliseconds(config.neighbor_expiry_ms);

    auto fresh = neighbor_table.AllFresh(now, expiry);   // [ENGINEERING] excludes
                                                         // stale neighbors

    std::vector<Candidate> scored;
    scored.reserve(fresh.size());
    for (const auto& n : fresh) {
        if (!n.reachable_to_dest) continue;   // [DERIVED] eligibility predicate
                                              // — must have a known route
                                              // toward `destination`

        uint8_t tq = calculate_tq(n.tq_local, n.tq_recv, n.f_asym,
                                    n.hop_penalty, config.tq_max);
        double fm = calculate_fade_margin(n.rx_power_dbm, config.sensitivity_dbm);
        uint8_t dp = distance_penalty(fm);
        double lq = calculate_joker_lq(tq, dp, config.tq_max);

        scored.push_back(Candidate{n.mac, lq, 0});
    }

    // [PAPER] sort descending by LQ; [ENGINEERING] tie-break ascending MAC
    std::sort(scored.begin(), scored.end(), [](const Candidate& a, const Candidate& b) {
        if (a.lq != b.lq) return a.lq > b.lq;
        return a.mac < b.mac;
    });

    if (scored.size() > candidate_count) {
        scored.resize(candidate_count);   // [PAPER] top Ncandidates
    }

    for (size_t i = 0; i < scored.size(); ++i) {
        scored[i].priority = static_cast<uint8_t>(i + 1);   // [PAPER] 1 = highest
    }

    return scored;
}

}  // namespace joker
