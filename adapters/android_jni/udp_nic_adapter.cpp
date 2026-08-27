#include "udp_nic_adapter.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <android/log.h>
#include <iostream>

#define LOG_TAG "JokerUdpNic"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace joker {
namespace android {

UdpNicAdapter::UdpNicAdapter(const std::string& bind_ip, uint16_t port, const joker::MacAddress& mac_address)
    : bind_ip_(bind_ip), port_(port), mac_address_(mac_address), socket_fd_(-1), running_(false) {
}

UdpNicAdapter::~UdpNicAdapter() {
    Stop();
}

void UdpNicAdapter::Start() {
    if (running_) return;

    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        LOGE("Failed to create UDP socket");
        return;
    }

    // Enable broadcast
    int broadcast_enable = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        LOGE("Failed to set SO_BROADCAST");
    }

    // Allow address reuse
    int reuse_enable = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_enable, sizeof(reuse_enable)) < 0) {
        LOGE("Failed to set SO_REUSEADDR");
    }

    sockaddr_in bind_addr{};
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(port_);
    // Bind to INADDR_ANY to receive broadcasts regardless of the interface
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY); 

    if (bind(socket_fd_, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        LOGE("Failed to bind UDP socket to port %d", port_);
        close(socket_fd_);
        socket_fd_ = -1;
        return;
    }

    running_ = true;
    receive_thread_ = std::thread(&UdpNicAdapter::ReceiveLoop, this);
    LOGI("UdpNicAdapter started on port %d with Virtual MAC: %02x:%02x:%02x:%02x:%02x:%02x", 
         port_, mac_address_.bytes()[0], mac_address_.bytes()[1], mac_address_.bytes()[2], 
         mac_address_.bytes()[3], mac_address_.bytes()[4], mac_address_.bytes()[5]);
}

void UdpNicAdapter::Stop() {
    if (!running_) return;
    
    LOGI("Stopping UdpNicAdapter...");
    running_ = false;

    if (socket_fd_ >= 0) {
        // Close the socket to interrupt the blocking recvfrom in the thread
        close(socket_fd_);
        socket_fd_ = -1;
    }

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    LOGI("UdpNicAdapter stopped.");
}

void UdpNicAdapter::RegisterReceiveCallback(ReceiveCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    receive_callback_ = std::move(callback);
}

joker::MacAddress UdpNicAdapter::GetMacAddress() const {
    return mac_address_;
}

void UdpNicAdapter::TransmitUnicast(const joker::MacAddress& destination, const std::vector<uint8_t>& frame) {
    // In a physical wireless medium, a unicast frame is physically broadcasted over the air.
    // Neighbors overhear it and drop it if the destination MAC doesn't match theirs.
    // Therefore, we simulate this physics by broadcasting all frames.
    SendUdpPacket(frame);
}

void UdpNicAdapter::TransmitBroadcast(const std::vector<uint8_t>& frame) {
    SendUdpPacket(frame);
}

void UdpNicAdapter::SendUdpPacket(const std::vector<uint8_t>& frame) {
    if (socket_fd_ < 0) return;

    sockaddr_in dest_addr{};
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port_);
    dest_addr.sin_addr.s_addr = inet_addr("255.255.255.255"); // Subnet broadcast

    ssize_t sent_bytes = sendto(socket_fd_, frame.data(), frame.size(), 0,
                                (struct sockaddr*)&dest_addr, sizeof(dest_addr));
                                
    if (sent_bytes < 0) {
        LOGE("Failed to send UDP packet: %s", strerror(errno));
    }
}

void UdpNicAdapter::ReceiveLoop() {
    std::vector<uint8_t> buffer(65535); // Max UDP size

    while (running_) {
        sockaddr_in sender_addr{};
        socklen_t sender_len = sizeof(sender_addr);
        
        ssize_t received_bytes = recvfrom(socket_fd_, buffer.data(), buffer.size(), 0,
                                          (struct sockaddr*)&sender_addr, &sender_len);
                                          
        if (received_bytes < 0) {
            if (running_) {
                LOGE("Error receiving UDP packet: %s", strerror(errno));
            }
            break; // Socket closed or error, exit loop
        }

        if (received_bytes > 0) {
            std::vector<uint8_t> frame(buffer.begin(), buffer.begin() + received_bytes);
            
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (receive_callback_) {
                // JOKER protocol expects:
                // 1. the frame
                // 2. our local MAC
                // 3. whether we are the candidate (we can pass true here and let JOKER's internal parser decide)
                // Actually, looking at interface.hpp:
                // 3. A boolean indicating if this local MAC was the intended link-layer candidate.
                // Since UDP doesn't have a MAC header, we just pass true. The JOKER header parser will drop if it's not actually for us.
                receive_callback_(frame, mac_address_, true);
            }
        }
    }
}

} // namespace android
} // namespace joker
