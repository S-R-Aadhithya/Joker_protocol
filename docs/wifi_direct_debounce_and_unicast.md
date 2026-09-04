# Implementation Complete: Debounce & Unicast Pivot

Both reliability fixes have been successfully implemented across the Kotlin and C++ codebases!

## 1. Debouncing `connectToPeer`
To prevent the Android OS state machine from crashing with an `ERROR (0)` due to overlapping `connect` calls, we implemented a strict state lock:

*   **Flag Added:** `isConnecting` flag in `WiFiDirectManager.kt`.
*   **Tap Rejection:** If `connectToPeer` is called while a connection is already in progress, it immediately returns, safely ignoring frantic taps.
*   **Auto-Reset:** The lock is intelligently released when the connection either succeeds or fails (via `onFailure` or the `CONNECTION_CHANGED` broadcast).
*   **Timeout Safety:** A 15-second `Handler` timeout was added to ensure the UI can never permanently freeze if the Android OS swallows a broadcast.

## 2. Unicast Auto-Learning in C++
To eliminate the ~50% packet loss associated with Layer-2 broadcasts (which lack hardware ACKs), we pivoted the C++ JOKER engine to 100% reliable Unicast:

*   **Auto-Capture in `ReceiveLoop`:** When `udp_nic_adapter.cpp` receives its first packet (even a broadcast fallback packet), it extracts the sender's IP using `inet_ntoa(sender_addr.sin_addr)` and caches it in `peer_ip_`.
*   **Dynamic Routing in `SendUdpPacket`:** The engine now seamlessly prioritizes Unicast routing. If `peer_ip_` is known, it sends the packet directly to that IP. If it's unknown (e.g., the first packet sent by the Group Owner), it falls back to a Subnet Broadcast to establish the initial handshake.
*   **Kotlin Injection:** Added `SetPeerIp(ip)` in `udp_nic_adapter.hpp` to allow the JNI to forcibly set the Group Owner's IP from the Client side immediately upon connection, eliminating the need for even the first fallback broadcast. This was fully wired up across the stack:
    *   **JNI Layer:** Exposed `Java_com_example_jokerprotocol_MainActivity_setPeerIp` in `joker_jni.cpp` to safely acquire the JOKER node mutex and pass the IP string down to the `UdpNicAdapter`.
    *   **Kotlin Layer:** Declared the `external fun setPeerIp(ip: String)` in `MainActivity.kt` and updated the `onStartProtocolClick` UI lambda to immediately invoke it for the Client side (`!isGroupOwner`) as soon as the protocol starts.
