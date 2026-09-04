#include <iostream>
#include <string>
#include <thread>
#include <memory>
#include <mutex>

#include "joker/neighbor.hpp"
#include "joker/dedup_cache.hpp"
#include "joker/timer_wheel.hpp"
#include "joker/coordinator.hpp"
#include "joker/metrics.hpp"
#include "joker/forwarding.hpp"
#include "adapters/android_jni/udp_nic_adapter.hpp"

using namespace joker;
using namespace joker::android; // Using the UDP adapter

struct JokerInstance {
    std::unique_ptr<UdpNicAdapter> nic;
    NeighborTable neighbors;
    DedupCache dedup;
    SimpleTimerWheel timer_wheel;
    std::unique_ptr<TimerCoordinator> coordinator;
    SimpleMetrics metrics;
    Config config;

    JokerInstance(const std::string& ip, uint16_t port, bool is_go) : dedup(1000, std::chrono::milliseconds(5000)) {
        // Generate a random Virtual MAC based on time to ensure uniqueness
        uint8_t rand_byte = static_cast<uint8_t>((time(nullptr) + port) % 255);
        MacAddress mac({0x02, 0x00, 0x00, 0x00, 0x00, rand_byte});

        // Use the UDP NIC adapter
        nic = std::make_unique<UdpNicAdapter>(ip, port, mac);
        coordinator = std::make_unique<TimerCoordinator>(timer_wheel);
        
        nic->RegisterReceiveCallback([this](const std::vector<uint8_t>& frame, const MacAddress& mac, bool is_cand) {
            process_received_frame(frame, mac, is_cand, neighbors, dedup, *coordinator, *nic, metrics, config);
            
            // Just for demonstration in CLI, print received messages
            size_t consumed = 0;
            auto hdr = deserialize_header(frame, consumed);
            if (hdr && frame.size() > consumed) {
                std::string msg(frame.begin() + consumed, frame.end());
                std::cout << "\n[Received from " << mac.ToString() << "]: " << msg << std::endl;
            } else {
                std::cout << "\n[Received RAW from " << mac.ToString() << "]: length=" << frame.size() << std::endl;
            }
            std::cout << "joker-cli> " << std::flush;
        });
        
        nic->Start();
        std::cout << "[INFO] JOKER Protocol Laptop Node started with MAC " << mac.ToString() << " on UDP port " << port << ".\n";
    }

    ~JokerInstance() {
        if (nic) {
            nic->Stop();
        }
    }
};

int main(int argc, char* argv[]) {
    uint16_t port = 5005;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    std::cout << "--- JOKER Protocol Laptop Node ---\n";
    std::cout << "Starting on port " << port << ".\n";
    
    // Bind to any address
    JokerInstance node("0.0.0.0", port, false);

    std::string input;
    while (true) {
        std::cout << "joker-cli> ";
        std::getline(std::cin, input);
        if (input == "exit" || input == "quit") {
            break;
        }
        if (!input.empty()) {
            JokerHeader hdr;
            hdr.type = PacketType::kUnicast;
            hdr.ttl = 32;
            hdr.packet_id = static_cast<uint32_t>(time(nullptr) ^ input.length());
            hdr.final_destination = MacAddress({0x02, 0x00, 0x00, 0x00, 0x00, 0xFF}); // Dummy Unicast MAC
            
            std::vector<uint8_t> wire;
            serialize_header(hdr, wire);
            wire.insert(wire.end(), input.begin(), input.end());
            
            node.nic->TransmitBroadcast(wire);
        }
    }

    std::cout << "Shutting down..." << std::endl;
    return 0;
}
