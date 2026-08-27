#pragma once
#include <cstdint>

namespace joker {

enum class CoordinationMode : uint8_t {
    kAckBased = 0,     // [PAPER]
    kTimerBased = 1,   // [PAPER]
};

struct Config {
    uint8_t  ttl_default          = 32;     // [PAPER] "usual figure of 32"
    uint16_t tq_max               = 255;    // [PAPER]
    uint8_t  candidate_count      = 2;      // [PAPER]-tunable; 2 is the experimentally
                                            // strongest default under heavy load
    CoordinationMode coordination = CoordinationMode::kTimerBased; // [ENGINEERING]
                                            // default choice; paper shows JOKER-timer
                                            // generally outperforming JOKER-ACK
    uint32_t twait_ms             = 50;     // [PAPER] best-tested value in the
                                            // paper's Nakagami-m video scenario;
                                            // NOT a universal optimum
    uint8_t  retry_limit          = 5;      // [PAPER]-tunable; paper's ablation
                                            // (Fig. 8) found 5 sufficient for
                                            // JOKER-timer to exceed 95% PDR
    uint32_t dedup_cache_capacity = 4096;   // [ENGINEERING] — not paper-specified
    uint32_t dedup_cache_ttl_ms   = 5000;   // [ENGINEERING]
    uint32_t neighbor_expiry_ms   = 6000;   // [ENGINEERING], nominally a few CMSI
                                            // periods; not paper-specified
    double   cmsi_min_s           = 1.5;    // [PAPER] floor at TP=0
    double   cmsi_max_s           = 30.0;   // [ENGINEERING] safety clamp, not
                                            // paper-specified
    double   sensitivity_dbm      = -74.0;  // [PAPER] example value for the
                                            // BCM4330 chip used in the paper's
                                            // simulation; MUST be set to the
                                            // real deployed NIC's datasheet value
    uint32_t ack_coordination_timeout_ms = 50; // [ENGINEERING] timeout for ACK coordination
};

}  // namespace joker
