# 07 — Source Code Reference & File Index

This document provides a comprehensive navigation map across the entire JOKER Protocol codebase, detailing the purpose, primary classes, and interactions of every file.

---

## 1. Directory Tree Overview

```
Joker_protocol/
├── include/joker/                 # C++ Core Public API Headers
│   ├── candidate.hpp              # Candidate selection algorithm
│   ├── coordinator.hpp            # Timer & ACK Coordinator interfaces
│   ├── dedup.hpp                  # DedupCache declaration
│   ├── forwarding.hpp             # Central RX decision pipeline
│   ├── header.hpp                 # Binary wire-format definitions
│   ├── interface.hpp              # Pure virtual NicAdapter interface
│   ├── mac_address.hpp            # 6-byte MAC address value type
│   ├── metrics.hpp                # SimpleMetrics counter collector
│   ├── neighbor.hpp               # NeighborTable & TQ link calculation
│   └── timer.hpp                  # Hierarchical SimpleTimerWheel
│
├── src/                           # C++ Core Implementation
│   ├── candidate.cpp              # Candidate ranking & distance penalty
│   ├── coordinator_ack.cpp        # Two-phase ACK coordination logic
│   ├── coordinator_timer.cpp      # Delay-slotted opportunistic timer logic
│   ├── dedup_cache.cpp            # O(1) hash map + LRU eviction list
│   ├── forwarding.cpp             # process_received_frame implementation
│   ├── header.cpp                 # Serializer and deserializer
│   ├── mac_address.cpp            # String parsing & formatting
│   ├── metrics.cpp                # Thread-safe counter maps
│   ├── neighbor_table.cpp         # Windowed OGM tracking & TQ scoring
│   └── timer_wheel.cpp            # Wheel slot scheduling & Tick() logic
│
├── adapters/                      # Platform Network Adapters
│   ├── android_jni/               # Android NDK Integration
│   │   ├── joker_jni.cpp          # JNI bridge, JVM lifecycle, and ticker
│   │   ├── udp_nic_adapter.hpp    # Virtual UDP NIC header
│   │   └── udp_nic_adapter.cpp    # Socket lifecycle, loopback drop, routing
│   └── linux_af_packet/           # Native Linux AF_PACKET Raw Sockets
│       ├── linux_af_packet.hpp    # Raw socket header
│       └── linux_af_packet.cpp    # ETH_P_JOKER & promiscuous mode capture
│
├── android/app/src/main/          # Android Jetpack Compose Application
│   ├── AndroidManifest.xml        # Permissions, MainActivity, FileProvider
│   ├── res/xml/file_paths.xml     # Scoped storage FileProvider paths
│   └── java/com/example/jokerprotocol/
│       ├── MainActivity.kt        # Compose UI, File Chunker, Chat bubbles
│       ├── WiFiDirectManager.kt   # P2P discovery, PBC pairing, Group formation
│       └── theme/                 # Material 3 styling and color schemes
│
├── cli/                           # Linux Terminal Executable
│   └── main.cpp                   # Standalone desktop CLI demonstration
│
└── tests/                         # Unit & Integration Test Suites
    ├── unit/                      # GoogleTest unit tests
    └── integration/               # Multi-hop mesh & penetration tests
```

---

## 2. Key File Deep-Dives

### 2.1 C++ Core (`include/` and `src/`)

#### [`include/joker/header.hpp`](file:///home/nkandasamy/Desktop/Joker_protocol/include/joker/header.hpp) & [`src/header.cpp`](file:///home/nkandasamy/Desktop/Joker_protocol/src/header.cpp)
- **Role**: Link-layer protocol header serialization.
- **Key Functions**:
  - `serialize_header(const JokerHeader& header, std::vector<uint8_t>& out)`: Appends the 13-byte fixed header + variable candidates.
  - `deserialize_header(const std::vector<uint8_t>& data, size_t& consumed)`: Validates bounds, validates packet type byte, and unpacks the header.
  - `joker_packet_id(const uint8_t* data, size_t len)`: Computes standard IEEE 802.3 CRC-32 checksum.

#### [`include/joker/forwarding.hpp`](file:///home/nkandasamy/Desktop/Joker_protocol/include/joker/forwarding.hpp) & [`src/forwarding.cpp`](file:///home/nkandasamy/Desktop/Joker_protocol/src/forwarding.cpp)
- **Role**: The central protocol decision pipeline.
- **Key Function**:
  - `process_received_frame(...)`: The choke point that parses the frame, verifies TTL $> 1$, checks for direct/lucky delivery, queries `DedupCache`, handles candidate classification, and hands off to the active `Coordinator`.

#### [`src/coordinator_timer.cpp`](file:///home/nkandasamy/Desktop/Joker_protocol/src/coordinator_timer.cpp)
- **Role**: Timer-based candidate election.
- **Key Functions**:
  - `OnCandidateReceivedDataPacket(...)`: Computes delay $T_{\text{wait}} \times (\text{rank} - 1)$ and schedules forwarding on `TimerWheel`. Decrements TTL before downstream transmission.
  - `HandleOverheardForward(uint32_t packet_id)`: Cancels pending timer when an overheard transmission from a higher-priority candidate is detected.

#### [`src/dedup_cache.cpp`](file:///home/nkandasamy/Desktop/Joker_protocol/src/dedup_cache.cpp)
- **Role**: $O(1)$ duplicate suppression with time-based TTL expiration.
- **Key Functions**:
  - `Contains(uint32_t id)`: Checks if a packet has been processed within the last 5 seconds.
  - `Insert(uint32_t id)`: Adds packet ID and maintains LRU capacity limits.

---

### 2.2 Adapters (`adapters/`)

#### [`adapters/android_jni/joker_jni.cpp`](file:///home/nkandasamy/Desktop/Joker_protocol/adapters/android_jni/joker_jni.cpp)
- **Role**: Bridge between the Android ART runtime (Kotlin) and the native C++ JOKER core.
- **Key Features**:
  - Maintains `JokerInstance` with a dedicated 10ms background timer wheel ticker thread.
  - Implements deterministic P2P MAC addressing (GO = `0x01`, CLI = `0x02`).
  - Pre-populates sender `DedupCache` to eliminate self-echoes.
  - Dispatches clean incoming packets to `MainActivity.onPacketReceived()`.

#### [`adapters/android_jni/udp_nic_adapter.cpp`](file:///home/nkandasamy/Desktop/Joker_protocol/adapters/android_jni/udp_nic_adapter.cpp)
- **Role**: Virtual network interface simulating wireless broadcast over Android UDP sockets.
- **Key Features**:
  - Drops local loopbacks matching `sender_ip == bind_ip_`.
  - Maintains a dynamic unicast peer routing table (`peer_ips_`).
  - Atomic socket FD with graceful `shutdown(SHUT_RDWR)` teardown.

---

### 2.3 Android Jetpack Compose App (`android/`)

#### [`android/app/src/main/java/com/example/jokerprotocol/MainActivity.kt`](file:///home/nkandasamy/Desktop/Joker_protocol/android/app/src/main/java/com/example/jokerprotocol/MainActivity.kt)
- **Role**: Modern user interface, file chunking engine, and media streaming controller.
- **Key Components**:
  - `sendMedia(fileName, data, mimeType)`: Coroutine-based 800-byte chunker with 8ms pacing.
  - `onPacketReceived(payload)`: Slices and reassembles `FT_META`, `FT_CHUNK`, and `FT_DONE` streams into completed files.
  - `MainUi`: Interactive Compose UI with tabs for P2P Chat/Media and Live Protocol Logs, inline image thumbnail rendering, animated transfer progress bars, and 1-tap demo photo/document generators.

#### [`android/app/src/main/java/com/example/jokerprotocol/WiFiDirectManager.kt`](file:///home/nkandasamy/Desktop/Joker_protocol/android/app/src/main/java/com/example/jokerprotocol/WiFiDirectManager.kt)
- **Role**: Wrapper around Android `WifiP2pManager`.
- **Key Features**:
  - Registers broadcast receivers with Android 14+ `RECEIVER_EXPORTED`.
  - Configures `groupOwnerIntent = 15` and `WpsInfo.PBC` for rapid zero-touch group formation.
  - Extracts Group Owner IP and local P2P interface addresses.
