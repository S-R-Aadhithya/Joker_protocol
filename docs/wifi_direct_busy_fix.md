# Wi-Fi Direct BUSY Error Fix (Android HAL)

## The Problem
On certain Android devices (notably Xiaomi/Redmi with MIUI/HyperOS, and some Samsung devices), the Wi-Fi Direct Hardware Abstraction Layer (HAL) can get stuck in a "busy" state. When this happens, calling `WifiP2pManager.discoverPeers` fails immediately, and the `ActionListener.onFailure` callback fires with `WifiP2pManager.BUSY` (Error Code 2).

This typically occurs if a previous discovery scan, connection attempt, or group formation didn't tear down properly due to a crash or improper lifecycle handling.

## The Solution

We implemented an aggressive state-reset sequence in `WiFiDirectManager.kt` to programmatically nuke the hung Wi-Fi Direct state without requiring the user to physically toggle their Wi-Fi switch off and on.

### 1. The Teardown Sequence
A new private method `resetWiFiDirectState()` was added to `WiFiDirectManager`. It executes the following teardown commands sequentially:
1.  `cancelConnect()`: Aborts any hung handshakes.
2.  `removeGroup()`: Dismantles any active SoftAP if the device was stuck as a Group Owner.
3.  `stopPeerDiscovery()`: Clears the discovery queue and cancels stuck scans.

### 2. Reflection: Nuking Persistent Groups
Android aggressively caches old Wi-Fi Direct connections as "Persistent Groups." Some OEMs refuse to initiate new scans if this list is full or corrupted. The method `deletePersistentGroup(Channel, int netId, ActionListener)` is hidden in the Android API (`@hide`). 

We used **Java Reflection** to invoke this method. Since we cannot query the exact Network IDs easily, the code safely loops from `0` to `31` and attempts to delete all possible cached persistent groups to ensure a clean slate.

### 3. Graceful Retry
The `discoverPeers()` method was updated to intercept the `WifiP2pManager.BUSY` error. 
*   On the first occurrence of the `BUSY` error, it automatically triggers `resetWiFiDirectState()`.
*   It waits 500ms for the HAL to process the teardown commands, and then automatically retries `discoverPeers()`.

## Files Modified
*   `android/app/src/main/java/com/example/jokerprotocol/WiFiDirectManager.kt`
