#include "udp_nic_adapter.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "JokerUdpNic"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(fmt, ...) do { printf("[INFO] " fmt "\n", ##__VA_ARGS__); } while(0)
#define LOGE(fmt, ...) do { fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__); } while(0)
#endif

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

    // Enable broadcast (keeping it just in case)
    int broadcast_enable = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        LOGE("Failed to set SO_BROADCAST");
    }

    // Allow address reuse
    int reuse_enable = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_enable, sizeof(reuse_enable)) < 0) {
        LOGE("Failed to set SO_REUSEADDR");
    }
    
#ifdef SO_REUSEPORT
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEPORT, &reuse_enable, sizeof(reuse_enable)) < 0) {
        LOGE("Failed to set SO_REUSEPORT");
    }
#endif

    // Removed IP_ADD_MEMBERSHIP and IP_MULTICAST_TTL because we are pivoting to Subnet Broadcast
    // to bypass Android's broken Multicast routing over Wi-Fi Direct interfaces.

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

void UdpNicAdapter::SetPeerIp(const std::string& ip) {
    std::lock_guard<std::mutex> lock(peer_ip_mutex_);
    peer_ip_ = ip;
    LOGI("Peer IP explicitly set to: %s", peer_ip_.c_str());
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
    std::string broadcast_ip = "255.255.255.255";
    if (!bind_ip_.empty() && bind_ip_ != "0.0.0.0") {
        size_t last_dot = bind_ip_.find_last_of('.');
        if (last_dot != std::string::npos) {
            broadcast_ip = bind_ip_.substr(0, last_dot) + ".255";
        }
    }
    
    std::string dest_ip;
    {
        std::lock_guard<std::mutex> lock(peer_ip_mutex_);
        dest_ip = peer_ip_;
    }

    if (!dest_ip.empty()) {
        dest_addr.sin_addr.s_addr = inet_addr(dest_ip.c_str());
    } else {
        dest_addr.sin_addr.s_addr = inet_addr(broadcast_ip.c_str()); // Subnet Broadcast
    }

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
            std::string sender_ip = inet_ntoa(sender_addr.sin_addr);
            if (sender_ip != bind_ip_ && sender_ip != "127.0.0.1") {
                std::lock_guard<std::mutex> lock(peer_ip_mutex_);
                if (peer_ip_.empty()) {
                    peer_ip_ = sender_ip;
                    LOGI("Auto-captured peer IP for Unicast routing: %s", peer_ip_.c_str());
                }
            }

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
