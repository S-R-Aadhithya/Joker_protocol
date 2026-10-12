#include <iostream>
#include <string>
#include <thread>
#include <memory>
#include <mutex>
#include <atomic>

#include "joker/neighbor.hpp"
#include "joker/dedup_cache.hpp"
#include "joker/timer_wheel.hpp"
#include "joker/coordinator.hpp"
#include "joker/metrics.hpp"
#include "joker/forwarding.hpp"
#include "joker/crc32.hpp"
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
    
    std::atomic<bool> ticker_running_{false};
    std::thread ticker_thread_;

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

        ticker_running_ = true;
        ticker_thread_ = std::thread([this]() {
            while (ticker_running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                timer_wheel.Tick();
            }
        });

        std::cout << "[INFO] JOKER Protocol Laptop Node started with MAC " << mac.ToString() << " on UDP port " << port << ".\n";
    }

    ~JokerInstance() {
        ticker_running_ = false;
        if (ticker_thread_.joinable()) {
            ticker_thread_.join();
        }
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
            static uint32_t cli_seq = 1;
            JokerHeader hdr;
            hdr.type = PacketType::kUnicast;
            hdr.ttl = 32;
            
            std::vector<uint8_t> id_bytes(input.begin(), input.end());
            uint32_t seq = cli_seq++;
            id_bytes.push_back(static_cast<uint8_t>((seq >> 24) & 0xFF));
            id_bytes.push_back(static_cast<uint8_t>((seq >> 16) & 0xFF));
            id_bytes.push_back(static_cast<uint8_t>((seq >> 8) & 0xFF));
            id_bytes.push_back(static_cast<uint8_t>(seq & 0xFF));
            hdr.packet_id = joker_packet_id(id_bytes.data(), id_bytes.size());
            hdr.final_destination = MacAddress({0x02, 0x00, 0x00, 0x00, 0x00, 0x01});
            
            std::vector<uint8_t> wire;
            serialize_header(hdr, wire);
            wire.insert(wire.end(), input.begin(), input.end());
            
            node.nic->TransmitBroadcast(wire);
        }
    }

    std::cout << "Shutting down..." << std::endl;
    return 0;
}
