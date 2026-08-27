#pragma once

#include "joker/interface.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

namespace joker {
namespace android {

class UdpNicAdapter : public joker::NicAdapter {
public:
    // Initializes the UDP adapter.
    // bind_ip: The IP address of the Wi-Fi Direct interface (e.g., "192.168.49.x")
    // port: The UDP port to use for JOKER traffic
    // mac_address: The virtual MAC address assigned to this node
    UdpNicAdapter(const std::string& bind_ip, uint16_t port, const joker::MacAddress& mac_address);
    ~UdpNicAdapter() override;

    // joker::NicAdapter interface
    void TransmitUnicast(const joker::MacAddress& destination, const std::vector<uint8_t>& frame) override;
    void TransmitBroadcast(const std::vector<uint8_t>& frame) override;
    void RegisterReceiveCallback(ReceiveCallback callback) override;
    void Start() override;
    void Stop() override;
    [[nodiscard]] joker::MacAddress GetMacAddress() const override;

private:
    void ReceiveLoop();
    void SendUdpPacket(const std::vector<uint8_t>& frame);

    std::string bind_ip_;
    uint16_t port_;
    joker::MacAddress mac_address_;
    
    int socket_fd_;
    std::atomic<bool> running_;
    std::thread receive_thread_;
    
    ReceiveCallback receive_callback_;
    std::mutex callback_mutex_;
};

} // namespace android
} // namespace joker
