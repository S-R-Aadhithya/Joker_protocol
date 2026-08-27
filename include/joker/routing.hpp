#pragma once
#include <cstdint>
#include "joker/neighbor.hpp"

namespace joker {

// [PAPER] Eq. (1): TQ = TQlocal * TQrecv * fasym * hop_penalty
// All inputs and the output are on the BATMAN 0..TQmax scale except
// fasym/hop_penalty which are dimensionless multipliers in [0,1].
uint8_t calculate_tq(uint8_t tq_local, uint8_t tq_recv,
                      double f_asym, double hop_penalty, uint16_t tq_max);
// [PAPER] FM = received power - sensitivity, both in dBm.
double calculate_fade_margin(double received_power_dbm, double sensitivity_dbm);

// [PAPER] Table I mapping. No hidden constants — thresholds and penalty
// values below are exactly the paper's.
inline uint8_t distance_penalty(double fade_margin_db) {
    if (fade_margin_db < 10.0) return 1;                         // [PAPER]
    if (fade_margin_db <= 20.0) return 3;                        // [PAPER]
    return 5;                                                    // [PAPER]
}

// [PAPER] Eq. (2): LQ = TQ * (TQmax - Distance_penalty) / TQmax
double calculate_joker_lq(uint8_t tq, uint8_t distance_penalty_value,
                           uint16_t tq_max);

}  // namespace joker
