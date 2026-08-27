#pragma once
#include <unordered_map>
#include <chrono>
#include <vector>
#include <cstdint>
#include <optional>
#include "joker/header.hpp"
#include "joker/mac_address.hpp"
#include "joker/neighbor.hpp"
#include "joker/config.hpp"
#include "joker/timer_wheel.hpp"

namespace joker {

class NicAdapter;
class Metrics;

class Coordinator {
public:
    virtual ~Coordinator() = default;
    
    virtual void OnCandidateReceivedDataPacket(
        const JokerHeader& header, const std::vector<uint8_t>& frame,
        size_t header_offset, const MacAddress& local_mac,
        NeighborTable& neighbors, NicAdapter& nic, Metrics& metrics,
        const Config& config) = 0;
        
    virtual void HandleAck(const JokerHeader& header,
                           const std::vector<uint8_t>& frame, size_t offset) = 0;
                           
    virtual void HandleForwardingMessage(const JokerHeader& header,
                                         const std::vector<uint8_t>& frame,
                                         size_t offset) = 0;
};

// [PAPER] Timer-based coordination (§III-C, Fig. 2(b))
class TimerCoordinator final : public Coordinator {
public:
    explicit TimerCoordinator(TimerWheel& wheel) : wheel_(wheel) {}

    void OnCandidateReceivedDataPacket(
        const JokerHeader& header, const std::vector<uint8_t>& frame,
        size_t header_offset, const MacAddress& local_mac,
        NeighborTable& neighbors, NicAdapter& nic, Metrics& metrics,
        const Config& config) override;

    // No ACK/Forwarding messages exist in timer mode — these are no-ops,
    // but overheard forwards of a DATA packet must be observed via a
    // separate passive-overhear hook, not via HandleAck/HandleForwardingMessage.
    void HandleAck(const JokerHeader&, const std::vector<uint8_t>&, size_t) override {}
    void HandleForwardingMessage(const JokerHeader&, const std::vector<uint8_t>&,
                                 size_t) override {}

    // Called by forwarding.cpp when a DATA packet's re-transmission (by
    // some other candidate) is overheard, keyed by packet_id.
    void HandleOverheardForward(uint32_t packet_id);

    // [ENGINEERING] Expose pending size for tests
    size_t GetPendingCount() const { return pending_.size(); }

// private:
    struct PendingForward {
        TimerId timer_id;
        std::vector<uint8_t> frame;  // retained for eventual forward
    };
    std::unordered_map<uint32_t, PendingForward> pending_;
    TimerWheel& wheel_;
};

// [PAPER] ACK-based coordination (§III-C, Fig. 2(a))
class AckCoordinator final : public Coordinator {
public:
    void OnCandidateReceivedDataPacket(
        const JokerHeader& header, const std::vector<uint8_t>& frame,
        size_t header_offset, const MacAddress& local_mac,
        NeighborTable& neighbors, NicAdapter& nic, Metrics& metrics,
        const Config& config) override;

    void HandleAck(const JokerHeader& header,
                   const std::vector<uint8_t>& frame, size_t offset) override;

    void HandleForwardingMessage(const JokerHeader& header,
                                 const std::vector<uint8_t>& frame,
                                 size_t offset) override;

    // [ENGINEERING] Method to process timeouts
    void ProcessTimeouts(const std::chrono::steady_clock::time_point& now);

    // [ENGINEERING] Method to add a pending coordination on the TX side
    void AddPending(uint32_t packet_id, const std::vector<uint8_t>& frame, const std::chrono::steady_clock::time_point& deadline);

    size_t GetPendingCount() const { return pending_.size(); }

private:
    struct PendingCoordination {
        uint32_t packet_id;
        std::vector<uint8_t> original_frame;   // retained for eventual forward
        std::chrono::steady_clock::time_point deadline; // [ENGINEERING] timeout
        std::optional<MacAddress> first_ack_sender;
    };
    std::unordered_map<uint32_t, PendingCoordination> pending_;
};

}  // namespace joker
