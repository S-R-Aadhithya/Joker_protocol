#include "joker/routing.hpp"
#include <algorithm>
#include <cmath>

namespace joker {

uint8_t calculate_tq(uint8_t tq_local, uint8_t tq_recv,
                      double f_asym, double hop_penalty, uint16_t tq_max) {
    // Normalize to [0,1], multiply, rescale to [0, tq_max], clamp, round.
    const double local_n = static_cast<double>(tq_local) / tq_max;
    const double recv_n  = static_cast<double>(tq_recv)  / tq_max;
    
    double tq = local_n * recv_n * f_asym * hop_penalty * tq_max;
    tq = std::clamp(tq, 0.0, static_cast<double>(tq_max));
    
    return static_cast<uint8_t>(std::lround(tq));
}
double calculate_fade_margin(double received_power_dbm, double sensitivity_dbm) {
    return received_power_dbm - sensitivity_dbm;   // [PAPER]
}

double calculate_joker_lq(uint8_t tq, uint8_t distance_penalty_value,
                           uint16_t tq_max) {
    if (tq_max == 0) return 0.0;   // [ENGINEERING] guard against div-by-zero;
                                     // tq_max is configuration-controlled and
                                     // should never legitimately be 0
    const double numerator = static_cast<double>(tq_max) -
                              static_cast<double>(distance_penalty_value);
    double lq = static_cast<double>(tq) * numerator / static_cast<double>(tq_max);
    // [ENGINEERING] numeric-range handling: distance_penalty_value in {1,3,5}
    // is always << tq_max (255 by default), so numerator stays positive; the
    // clamp below is a defensive guard, not something the paper's numbers
    // would normally trigger.
    return std::clamp(lq, 0.0, static_cast<double>(tq_max));
}

}  // namespace joker
