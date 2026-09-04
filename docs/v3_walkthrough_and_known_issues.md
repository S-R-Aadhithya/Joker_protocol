# JOKER Protocol v3.0 - Walkthrough and Known Issues

## 1. What was accomplished in v3.0
We successfully stabilized the Wi-Fi Direct (P2P) integration between Android and the C++ Engine (JOKER Protocol). This release addressed two critical flaws in Android's P2P networking stack:

### A. UI and Connection State Debouncing
- **Issue:** Tapping a peer multiple times or initiating a connection while Android's `WifiP2pManager` was still clearing a previous connection would cause the HAL to crash with `ERROR (0)`, aggressively wiping the peer list and dropping the UI.
- **Fix:** We introduced a robust state lock (`isConnecting` flag) in `WiFiDirectManager.kt`. Overlapping connection attempts are safely ignored until the Android OS broadcasts a definitive success (`CONNECTION_CHANGED`) or failure (`onFailure`). A 15-second `Handler` timeout was also added as a fail-safe against missed broadcasts.

### B. Reliable Unicast Routing
- **Issue:** Due to Android's strict firewall rules blocking standard IGMP Multicast over `p2p-wlan0`, we previously pivoted to Subnet Broadcast (`255.255.255.255`). However, MAC-layer broadcast frames in 802.11 lack hardware acknowledgments, resulting in ~50% packet loss over the air (e.g., messages occasionally dropping silently).
- **Fix:** We implemented a 100% reliable Auto-Learning Unicast system in C++. 
  - The C++ `UdpNicAdapter` dynamically caches the `peer_ip` as soon as it receives a packet.
  - The Kotlin UI injects the Group Owner's IP directly into the C++ engine via a new JNI method (`setPeerIp`) the second the protocol starts on the Client node. 
  - This guarantees that both nodes bypass UDP broadcast entirely and route all traffic using Unicast IP, letting the physical Wi-Fi chip handle packet retries.

## 2. Remaining Issues & Next Steps

While the core connection and messaging bridge is now fully functional, the following quirks remain:

### Asymmetric Discovery
- **Behavior:** Often, Phone A can see Phone B in the UI, but Phone B cannot see Phone A.
- **Cause:** This is a physics-level idiosyncrasy of Wi-Fi Direct. Android devices passively listen and actively beacon on channels 1, 6, and 11. They frequently miss each other's beacons.
- **Workaround:** As long as *one* device sees the other, the connection can be successfully initiated from that device.

### Hard Disconnects vs Graceful Teardown
- **Behavior:** We have not fully hardened the disconnection lifecycle. If a user forcefully kills the app or toggles their Wi-Fi switch, the peer might remain stuck in a "Connected" state in the OS's Wi-Fi Direct settings.
- **Next Steps:** Implement a proper lifecycle observer that forcefully calls `removeGroup()` when the app enters `onDestroy()` or when the JOKER Protocol is stopped, ensuring the Android P2P HAL is cleanly torn down.

### Mesh Routing Expansion
- **Next Steps:** The C++ backend currently caches a single `peer_ip_` in `UdpNicAdapter`. To support true multi-hop mesh networks, this must be expanded into a routing table mapped to MAC addresses.
