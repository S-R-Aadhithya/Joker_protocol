# 05 — Implementation Architecture & Engineering Resolutions

This document provides a technical deep-dive into the C++20 core engine implementation (`libjoker`), critical concurrency bugs and race conditions identified and resolved during development, and the design of the platform network adapters.

---

## 1. C++20 Core Library Architecture (`libjoker`)

The JOKER protocol engine is implemented in modern, idiomatic C++20 with zero third-party dependencies outside standard POSIX and C++ libraries.

```
                    ┌────────────────────────┐
                    │      JokerInstance     │
                    │ (Lifecycle & Threading)│
                    └───────────┬────────────┘
                                │
        ┌───────────────────────┼──────────────────────┐
        ▼                       ▼                      ▼
┌───────────────┐       ┌───────────────┐      ┌───────────────┐
│  TimerWheel   │       │   DedupCache  │      │ NeighborTable │
│ (10ms Ticker) │       │ (O(1) Map+LRU)│      │  (TQ Metrics) │
└───────┬───────┘       └───────┬───────┘      └───────┬───────┘
        │                       │                      │
        └───────────────────────┼──────────────────────┘
                                ▼
                    ┌────────────────────────┐
                    │ process_received_frame │
                    │   (Central Pipeline)   │
                    └───────────┬────────────┘
                                ▼
                    ┌────────────────────────┐
                    │   TimerCoordinator     │
                    │(Thread-Safe Forwarding)│
                    └───────────┬────────────┘
                                ▼
                    ┌────────────────────────┐
                    │   NicAdapter (L2/UDP)  │
                    └────────────────────────┘
```

### Core Subsystems:
- [`SimpleTimerWheel`](file:///home/nkandasamy/Desktop/Joker_protocol/include/joker/timer.hpp): Hierarchical delay timer scheduler supporting $O(1)$ addition, deletion, and tick advancement.
- [`DedupCache`](file:///home/nkandasamy/Desktop/Joker_protocol/include/joker/dedup.hpp): Sliding-window hash table + LRU eviction list for instantaneous suppression of seen packet IDs.
- [`TimerCoordinator`](file:///home/nkandasamy/Desktop/Joker_protocol/include/joker/coordinator.hpp): Schedules candidate forwarders and provides `HandleOverheardForward()` for early suppression.
- [`NeighborTable`](file:///home/nkandasamy/Desktop/Joker_protocol/include/joker/neighbor.hpp): Tracks link delivery ratios, computes TQ, and sorts candidates for every destination.

---

## 2. Engineering Resolutions & Race Condition Fixes

During rigorous static analysis, integration testing, and live hardware deployment, several critical production bugs and race conditions were identified and resolved:

### 2.1 Multithreaded Concurrency Race in `pending_` Forwarding Maps
- **The Bug**:
  In `TimerCoordinator` and `AckCoordinator`, incoming frames on the network worker thread inserted frames into `std::unordered_map<uint32_t, PendingForward> pending_`. Concurrently, the timer wheel ticker thread and overhearing cancellation routines were reading and erasing elements from `pending_` without synchronization.
- **The Resolution**:
  Introduced `std::mutex pending_mutex_` and wrapped all map insertions, lookups, and erasures with `std::lock_guard<std::mutex> lock(pending_mutex_)`.

### 2.2 Production Timer Stall (`timer_wheel.Tick()` Inactivity)
- **The Bug**:
  In earlier builds, `timer_wheel.Tick()` was only driven inside synthetic unit test loops. In production Android JNI and CLI binaries, no background event loop or thread was ticking the wheel. As a result, scheduled timers never expired, and secondary candidate failovers never fired.
- **The Resolution**:
  Implemented an active worker thread inside `JokerInstance`:
  ```cpp
  ticker_running_ = true;
  ticker_thread_ = std::thread([this]() {
      while (ticker_running_) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          timer_wheel.Tick();
      }
  });
  ```

### 2.3 Packet ID Collision in JNI
- **The Bug**:
  `joker_jni.cpp` originally generated packet IDs using `time(nullptr) ^ msg.length()`. In high-throughput messaging or burst transmissions within the same second, multiple packets generated identical IDs, causing the receiving `DedupCache` to drop legitimate packets as duplicates.
- **The Resolution**:
  Replaced with standard CRC-32 computed over payload bytes combined with a monotonic atomic 32-bit counter (`g_seq_counter.fetch_add(1)`).

### 2.4 Unbounded Memory Leak in UI Log Monitor
- **The Bug**:
  In `MainActivity.kt`, `_nativeLogs` was stored as an unbounded `List<String>`. Every incoming packet triggered an $O(N)$ list copy, causing rapid memory allocation, Dalvik heap growth, and severe garbage collection stutter after several minutes of traffic.
- **The Resolution**:
  Capped the list to 300 entries using `.drop(logs.size - 299)`, stabilizing memory consumption at ~70 MB indefinitely.

### 2.5 Local Subnet Broadcast Loopback Suppression
- **The Bug**:
  When transmitting UDP packets to `192.168.49.255`, the Linux network stack delivered an immediate local copy of the broadcast frame back to the sender's own listening socket on port 5005.
- **The Resolution**:
  Added sender IP filtering in `UdpNicAdapter::ReceiveLoop`:
  ```cpp
  if (!bind_ip_.empty() && bind_ip_ != "0.0.0.0" && sender_ip == bind_ip_) {
      continue; // Drop self-loopback
  }
  ```

### 2.6 Deterministic MAC Role Assignment for Direct Delivery
- **The Bug**:
  In early P2P tests, both phones generated random MACs, but sent packets with a dummy destination MAC `0x01`. Because neither device matched `0x01`, both devices treated themselves as intermediate candidate relays and re-broadcasted the packet back over the air.
- **The Resolution**:
  Assigned deterministic MAC roles in direct P2P topology:
  - Group Owner (GO): MAC `02:00:00:00:00:01`
  - Client (CLI): MAC `02:00:00:00:00:02`
  - Packets from GO target `0x02`, and packets from CLI target `0x01`. Direct delivery (`is_lucky_long_or_direct_delivery == true`) succeeds immediately, and spurious relay re-broadcasts are eliminated.

---

## 3. Network Interface Adapters

JOKER abstracts all physical transport behind the pure virtual interface [`NicAdapter`](file:///home/nkandasamy/Desktop/Joker_protocol/include/joker/interface.hpp):

```cpp
class NicAdapter {
public:
    virtual ~NicAdapter() = default;
    virtual void TransmitUnicast(const MacAddress& destination, const std::vector<uint8_t>& frame) = 0;
    virtual void TransmitBroadcast(const std::vector<uint8_t>& frame) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual MacAddress GetMacAddress() const = 0;
    void RegisterReceiveCallback(ReceiveCallback callback);
};
```

### 3.1 Linux AF_PACKET Raw Socket Adapter
- **Implementation**: [`adapters/linux_af_packet/linux_af_packet.cpp`](file:///home/nkandasamy/Desktop/Joker_protocol/adapters/linux_af_packet/linux_af_packet.cpp)
- **Mechanism**: Opens a raw packet socket: `socket(AF_PACKET, SOCK_RAW, htons(ETH_P_JOKER))`.
- **Promiscuous Mode**: Sets `PACKET_MR_PROMISC` via `setsockopt(SOL_PACKET, PACKET_ADD_MEMBERSHIP)` to capture all over-the-air 802.11 frames, enabling overhearing of packets not addressed to the local MAC.

### 3.2 Android Virtual UDP NIC Adapter
- **Implementation**: [`adapters/android_jni/udp_nic_adapter.cpp`](file:///home/nkandasamy/Desktop/Joker_protocol/adapters/android_jni/udp_nic_adapter.cpp)
- **Rationale**: Android unrooted devices restrict raw `AF_PACKET` sockets. The `UdpNicAdapter` runs over standard UDP sockets on port 5005, simulating the wireless broadcast medium using Wi-Fi Direct subnet broadcasts (`192.168.49.255`) and unicast peer tables.
- **Thread Safety**: Uses atomic socket file descriptors and `shutdown(SHUT_RDWR)` to ensure non-blocking worker thread termination without double-free errors.
