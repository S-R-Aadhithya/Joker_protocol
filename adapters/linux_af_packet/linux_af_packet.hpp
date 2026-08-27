#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <thread>
#include <atomic>
#include "joker/interface.hpp"
#include "joker/mac_address.hpp"

namespace joker {

class LinuxAfPacketAdapter : public NicAdapter {
public:
    // interface_name: e.g., "wlan0", "eth0"
    explicit LinuxAfPacketAdapter(std::string interface_name);
    ~LinuxAfPacketAdapter() override;

    void TransmitUnicast(const MacAddress& destination, const std::vector<uint8_t>& frame) override;
    void TransmitBroadcast(const std::vector<uint8_t>& frame) override;

    void RegisterReceiveCallback(ReceiveCallback callback) override;

    void Start() override;
    void Stop() override;

    [[nodiscard]] MacAddress GetMacAddress() const override;

private:
    void ReceiveLoop();
    void SetPromiscuousMode(bool enable);

    std::string interface_name_;
    int socket_fd_;
    int ifindex_;
    MacAddress mac_;

    ReceiveCallback callback_;
    std::atomic<bool> running_;
    std::thread rx_thread_;

    // To properly format the raw ethernet frame.
    void TransmitRaw(const MacAddress& dest, const std::vector<uint8_t>& payload);
};

}  // namespace joker
