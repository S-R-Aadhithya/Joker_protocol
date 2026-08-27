#ifdef __linux__
#include "adapters/linux_af_packet/linux_af_packet.hpp"

#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <poll.h>
#include <iostream>

namespace joker {

// Custom EtherType for JOKER frames to avoid conflicting with IPv4/IPv6 etc.
constexpr uint16_t ETH_P_JOKER = 0x88B5; // 802.1 Local Experimental 1 (or similar)

LinuxAfPacketAdapter::LinuxAfPacketAdapter(std::string interface_name)
    : interface_name_(std::move(interface_name)), socket_fd_(-1), ifindex_(-1), running_(false) {
    
    // Create raw socket
    socket_fd_ = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_JOKER));
    if (socket_fd_ < 0) {
        throw std::runtime_error("Failed to create AF_PACKET socket (requires root/CAP_NET_RAW)");
    }

    // Get interface index
    struct ifreq ifr = {};
    std::strncpy(ifr.ifr_name, interface_name_.c_str(), IFNAMSIZ - 1);
    if (ioctl(socket_fd_, SIOCGIFINDEX, &ifr) < 0) {
        close(socket_fd_);
        throw std::runtime_error("Failed to get interface index for " + interface_name_);
    }
    ifindex_ = ifr.ifr_ifindex;

    // Get MAC address
    if (ioctl(socket_fd_, SIOCGIFHWADDR, &ifr) < 0) {
        close(socket_fd_);
        throw std::runtime_error("Failed to get MAC address for " + interface_name_);
    }
    std::array<uint8_t, 6> mac_bytes;
    std::memcpy(mac_bytes.data(), ifr.ifr_hwaddr.sa_data, 6);
    mac_ = MacAddress(mac_bytes);

    // Bind socket to interface
    struct sockaddr_ll sll = {};
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifindex_;
    sll.sll_protocol = htons(ETH_P_JOKER);
    if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&sll), sizeof(sll)) < 0) {
        close(socket_fd_);
        throw std::runtime_error("Failed to bind AF_PACKET socket to interface");
    }
}

LinuxAfPacketAdapter::~LinuxAfPacketAdapter() {
    Stop();
    if (socket_fd_ >= 0) {
        close(socket_fd_);
    }
}

MacAddress LinuxAfPacketAdapter::GetMacAddress() const {
    return mac_;
}

void LinuxAfPacketAdapter::SetPromiscuousMode(bool enable) {
    struct ifreq ifr = {};
    std::strncpy(ifr.ifr_name, interface_name_.c_str(), IFNAMSIZ - 1);
    
    if (ioctl(socket_fd_, SIOCGIFFLAGS, &ifr) < 0) {
        return;
    }
    
    if (enable) {
        ifr.ifr_flags |= IFF_PROMISC;
    } else {
        ifr.ifr_flags &= ~IFF_PROMISC;
    }
    
    if (ioctl(socket_fd_, SIOCSIFFLAGS, &ifr) < 0) {
        std::cerr << "Warning: Failed to set promiscuous mode\n";
    }
}

void LinuxAfPacketAdapter::RegisterReceiveCallback(ReceiveCallback callback) {
    callback_ = std::move(callback);
}

void LinuxAfPacketAdapter::Start() {
    if (running_) return;
    
    SetPromiscuousMode(true);
    running_ = true;
    rx_thread_ = std::thread(&LinuxAfPacketAdapter::ReceiveLoop, this);
}

void LinuxAfPacketAdapter::Stop() {
    if (!running_) return;
    running_ = false;
    
    // Wake up poll
    shutdown(socket_fd_, SHUT_RDWR);
    
    if (rx_thread_.joinable()) {
        rx_thread_.join();
    }
    SetPromiscuousMode(false);
}

void LinuxAfPacketAdapter::TransmitRaw(const MacAddress& dest, const std::vector<uint8_t>& payload) {
    if (!running_) return;

    std::vector<uint8_t> frame(14 + payload.size());
    
    // Destination MAC
    std::memcpy(frame.data(), dest.bytes().data(), 6);
    // Source MAC
    std::memcpy(frame.data() + 6, mac_.bytes().data(), 6);
    // EtherType
    uint16_t eth_type = htons(ETH_P_JOKER);
    std::memcpy(frame.data() + 12, &eth_type, 2);
    // Payload
    std::memcpy(frame.data() + 14, payload.data(), payload.size());

    struct sockaddr_ll sll = {};
    sll.sll_ifindex = ifindex_;
    sll.sll_halen = ETH_ALEN;
    std::memcpy(sll.sll_addr, dest.bytes().data(), 6);

    sendto(socket_fd_, frame.data(), frame.size(), 0,
           reinterpret_cast<struct sockaddr*>(&sll), sizeof(sll));
}

void LinuxAfPacketAdapter::TransmitUnicast(const MacAddress& destination, const std::vector<uint8_t>& frame) {
    TransmitRaw(destination, frame);
}

void LinuxAfPacketAdapter::TransmitBroadcast(const std::vector<uint8_t>& frame) {
    std::array<uint8_t, 6> bcast = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    TransmitRaw(MacAddress(bcast), frame);
}

void LinuxAfPacketAdapter::ReceiveLoop() {
    std::vector<uint8_t> buffer(65536);
    struct pollfd pfd = {};
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;

    while (running_) {
        int ret = poll(&pfd, 1, 100); // 100ms timeout to check running_
        if (ret < 0) {
            if (errno == EINTR) continue;
            break; // Error
        }
        if (ret == 0) continue; // Timeout

        if (pfd.revents & POLLIN) {
            struct sockaddr_ll sll = {};
            socklen_t sll_len = sizeof(sll);
            ssize_t len = recvfrom(socket_fd_, buffer.data(), buffer.size(), 0,
                                   reinterpret_cast<struct sockaddr*>(&sll), &sll_len);
            
            if (len > 14) {
                // Parse ethernet header
                std::array<uint8_t, 6> dest_mac_bytes;
                std::memcpy(dest_mac_bytes.data(), buffer.data(), 6);
                MacAddress dest_mac(dest_mac_bytes);
                
                uint16_t eth_type;
                std::memcpy(&eth_type, buffer.data() + 12, 2);
                
                if (ntohs(eth_type) == ETH_P_JOKER) {
                    bool is_candidate = (dest_mac == mac_);
                    
                    std::vector<uint8_t> payload(buffer.begin() + 14, buffer.begin() + len);
                    if (callback_) {
                        callback_(payload, mac_, is_candidate);
                    }
                }
            }
        }
    }
}

}  // namespace joker
#endif // __linux__
