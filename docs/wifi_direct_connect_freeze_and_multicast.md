# Wi-Fi Direct: `connect()` Freeze and Multicast Drops

This document outlines the diagnosis and fixes for two critical issues encountered during the development of the Wi-Fi Direct Android app:
1. Hardware Negotiation Freeze during `connect()`
2. Multicast packets dropping over the `p2p-wlan0` interface

## Issue 1: Hardware Negotiation Freeze (`connect()`)

### The Problem
When the app is killed mid-connection or during group negotiation, the underlying `WifiP2pService` (and the `wpa_supplicant` driver) doesn't automatically abort the attempt. The OS gets stuck waiting for a Group Owner handshake. Upon restarting the app, subsequent calls to `WifiP2pManager.connect()` are silently ignored because the HAL believes a negotiation is already in progress, resulting in a frozen state where the `WIFI_P2P_CONNECTION_CHANGED_ACTION` broadcast never fires.

### The Solution
We updated `WiFiDirectManager.kt` to pre-emptively flush the state machine before initiating a connection:
*   Added an explicit call to `manager.cancelConnect()` right before attempting a new connection.
*   The actual `connect()` handshake logic was moved into a helper method (`executeConnect()`), which is strictly invoked inside the `onSuccess` or `onFailure` callback of the `cancelConnect()` call. 
*   This guarantees the Android state machine is wiped clean before a new P2P connection begins, allowing users to reconnect immediately after a force-quit.

## Issue 2: Multicast Packets Dropping Over Wi-Fi Direct

### The Problem
During a successful Wi-Fi Direct connection, C++ UDP multicast sockets (e.g., binding to `239.255.255.250`) may fail to transmit or receive packets across the P2P interface. 

This happens due to the fragmented nature of Android networking:
1.  **MulticastLock Quirk:** The `WifiManager.MulticastLock` applies globally but is often hardcoded to the infrastructure interface (`wlan0`) by OEMs, leaving the `p2p0` interface locked out by the Android firewall.
2.  **OS Routing Table Priority:** Android's kernel routing table defaults to sending `224.0.0.0/4` (all multicast traffic) out of the default gateway (cellular or `wlan0`), entirely bypassing the Wi-Fi Direct interface, regardless of the `IP_MULTICAST_IF` bind.

### C++ Engine Fixes and Recommendations

To resolve this at the C++ socket layer, you must bypass Android's unreliable multicast routing:

1.  **Force Interface Binding (`SO_BINDTODEVICE`):**
    Binding to an IP is insufficient on Android. You must explicitly bind the UDP socket to the physical P2P network interface string (e.g., `p2p-wlan0`):
    ```cpp
    const char* ifaceName = "p2p-wlan0"; // Must be fetched dynamically from Android Network APIs
    setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, ifaceName, strlen(ifaceName));
    ```

2.  **Architectural Pivot (Highly Recommended):**
    Because standard Wi-Fi Direct is strictly a single-subnet Peer-to-Peer topology, IP Multicast introduces unnecessary complexities.
    *   **Use Subnet Broadcast (`192.168.49.255`):** By sending UDP packets to the subnet broadcast address, or the global `255.255.255.255`, you completely bypass IGMP snooping bugs while still reaching all devices on the P2P network.
    *   **Use Unicast:** The Group Owner always operates on `192.168.49.1`. The Client can Unicast "hello" packets directly to the GO, and the GO can extract the Client's IP from `recvfrom`. Unicast is 100% reliable and immune to multicast drops on Android.
