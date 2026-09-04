# Comprehensive Root Cause Analysis: Android Wi-Fi Direct "0-Peer" Silent Failure

This document provides an extremely thorough, deep-dive diagnosis into why an Android device successfully executes a Wi-Fi Direct scan (`WIFI_P2P_DISCOVERY_STARTED` fires) but consistently fails to trigger `WIFI_P2P_PEERS_CHANGED_ACTION` (yielding 0 peers). 

When debugging Wi-Fi Direct, you must analyze the stack across three distinct layers: the **Software Layer (Android Framework)**, the **Driver Layer (Hardware Concurrency)**, and the **Environmental Layer (Enterprise RF interference)**.

---

## 1. The Software Layer (Android 13+ Framework & Permissions)

Android 13 (API 33) drastically changed how Wi-Fi scanning permissions work, introducing edge cases that result in silent failures rather than outright crashes.

### The Decoupling Quirk (`NEARBY_WIFI_DEVICES` vs `ACCESS_FINE_LOCATION`)
Google introduced `NEARBY_WIFI_DEVICES` to allow apps to scan for Wi-Fi Direct peers without needing location permissions. However, there is a massive hidden caveat:
*   **Legacy HAL Implementations:** The Android Framework (Java/Kotlin) knows about the new permission, but the underlying Hardware Abstraction Layer (HAL - the C++ driver provided by Qualcomm, Broadcom, etc.) often does not. If your test device *upgraded* to Android 13/14 rather than launching with it natively, the legacy HAL may secretly still require GPS to be physically turned on and `ACCESS_FINE_LOCATION` to be granted.
*   **Silent Redaction:** If the permission matrix is incomplete at the driver level, the Android Framework (`WifiP2pService`) does not crash your app. Instead, to prevent privacy leaks, it silently drops the MAC addresses from the IPC payload. Your app receives a successful scan notification, but the peer list is scrubbed clean (0 peers), meaning `WIFI_P2P_PEERS_CHANGED_ACTION` is optimized out.

### Why Polling is an Anti-Pattern
You asked if you should use `manager.requestPeers()` on a timer to poll the OS instead of relying on the broadcast.
*   **Do NOT poll.** The `WifiP2pManager` does not execute a new hardware scan when you call `requestPeers()`. It merely queries a cached state map inside the system `WifiP2pService`. 
*   If the hardware (via `wpa_supplicant`) detects a peer, it fires an asynchronous event to the framework, which updates the cache and broadcasts `WIFI_P2P_PEERS_CHANGED_ACTION`. Polling a timer will only retrieve the same empty cache repeatedly, burning CPU and battery.

---

## 2. The Driver Layer (Wi-Fi Interface Concurrency & DFS)

Your testing environment—a college dorm—is highly relevant to driver-level failures.

### The DFS Concurrency Bottleneck
Enterprise networks heavily utilize **5GHz DFS (Dynamic Frequency Selection)** channels to handle high density. Wi-Fi Direct devices (like TVs and printers) almost exclusively advertise their presence on the 2.4GHz "Social Channels" (Channels 1, 6, and 11).
*   **Off-Channel Scanning:** Most Android phones have a single Wi-Fi radio. If your phone is connected to the college 5GHz Wi-Fi, it must perform "off-channel scanning" to search for P2P devices.
*   **The Physics:** The Wi-Fi radio tunes away from the 5GHz enterprise network to 2.4GHz for a tiny window (usually 30-50 milliseconds) to listen for P2P Probe Responses, then immediately snaps back to 5GHz so it doesn't drop the infrastructure connection.
*   **The Result:** Because the listening windows are so microscopically small, the mathematical probability of intercepting a peer's P2P Beacon is extremely low. The framework successfully starts the scan, but the driver physically cannot hear the peers due to time-slicing constraints.

---

## 3. The Environmental Layer (Enterprise WIPS)

You asked: *"Do college Enterprise networks suppress P2P beacons, or does Wi-Fi Direct bypass the infrastructure access point entirely?"*

Wi-Fi Direct operates entirely at Layer 2 via 802.11 Action Frames. It *should* bypass the access point. However, Enterprise Networks actively attack P2P networks.

### WIPS (Wireless Intrusion Prevention Systems)
College networks employ aggressive WIPS infrastructure (e.g., Cisco CleanAir, Aruba AirWave) to protect their airspace. 
*   **Rogue AP Detection:** When a Smart TV or printer in a dorm emits a Wi-Fi Direct beacon (advertising itself as a Group Owner), the College WIPS detects an unauthorized BSSID transmitting in its airspace.
*   **Deauthentication Spoofing (The Attack):** The Enterprise Access Point retaliates by blasting IEEE 802.11 Deauthentication frames, spoofing the MAC address of the TV/Printer. 
*   **Suppression:** Any device trying to connect to the TV is instantly disconnected. More importantly, modern Smart TVs and printers detect this jamming and will automatically shut down their Wi-Fi Direct radios entirely to comply with network conditions. **The peers are missing from your scan because the college network literally forced them off the air.**

---

## 4. The Definitive Testing Methodology

To prove exactly where the failure is occurring, you must execute a strict variable-isolation test. This will definitively prove whether the 0-peer result is a bug in your code or a hostility in the environment.

### Phase 1: Total Network Isolation (Eliminate Concurrency Constraints)
1. On your primary Android testing device, turn on **Airplane Mode**.
2. Turn **Wi-Fi ON**, but ensure you are **Disconnected** from the college network (use "Forget Network" if necessary).
3. Ensure **Location (GPS)** is toggled ON.
*This forces the Wi-Fi HAL to dedicate 100% of its radio time to the P2P scan across all channels without 5GHz hopping.*

### Phase 2: Create a Guaranteed Target (Eliminate WIPS Interference)
Do not rely on dorm TVs or printers; their radios might be suppressed by WIPS.
1. Take a **second Android phone** (Phone B).
2. Go to `Settings -> Network & Internet -> Wi-Fi -> Wi-Fi Preferences -> Wi-Fi Direct`.
3. Leave Phone B's screen ON and physically sitting on that exact settings page.
*When an Android phone is on the Wi-Fi Direct settings page, the OS forces the Wi-Fi HAL into the `Search/Listen` state, actively emitting P2P Probe Responses at high frequency.*

### Phase 3: Execution
1. Run your Kotlin app on your primary device.
2. Trigger `manager.discoverPeers()`.

### The Verdict
*   **If your app discovers Phone B:** Your permissions, Intent Filters, and Kotlin code are **flawless**. The 0-peer issue is purely environmental—caused by Enterprise DFS concurrency limits or WIPS suppression of dorm devices.
*   **If your app STILL finds 0 peers:** You have a hard software bug. The device is silently filtering the list due to a mismatched permission (likely a HAL requesting legacy Location permissions), or your `BroadcastReceiver` is not dynamically registered correctly for Android 13+ context limits.
