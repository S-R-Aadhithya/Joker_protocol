# JOKER Protocol: Wi-Fi Direct (WifiP2pManager) Diagnostic Checklist

When `discoverPeers()` returns `onSuccess()` but `WIFI_P2P_PEERS_CHANGED_ACTION` never fires, it almost always means the Android Framework accepted the request, but the underlying Wi-Fi hardware (or `wpa_supplicant`) silently dropped the scan, or the peer list simply didn't change (0 to 0).

Here is the deep-dive diagnostic on why this happens and the code to reveal the OS's internal state.

## 1. The `onSuccess()` Lie
The `onSuccess` callback in `discoverPeers` is notoriously misleading. It does **not** mean the device is actively scanning. It simply means the inter-process communication (IPC) to the Android `WifiP2pService` was successful, and the request was queued. 

To know if the hardware *actually* started scanning, you **must** listen to `WIFI_P2P_DISCOVERY_CHANGED_ACTION`. If you never receive the `WIFI_P2P_DISCOVERY_STARTED` extra, the scan died at the hardware level.

## 2. Why would the hardware silently drop the scan?
*   **Wi-Fi Interface Concurrency (The DFS Problem):** You mentioned being in a college dorm. Enterprise Wi-Fi heavily utilizes 5GHz DFS (Dynamic Frequency Selection) channels. Many Android Wi-Fi chips cannot simultaneously maintain a connection on a DFS channel and perform an active Wi-Fi Direct scan (which typically probes 2.4GHz channels 1, 6, and 11). The OS will prioritize the existing infrastructure connection and silently drop the P2P scan.
*   **Mobile Hotspot is ON:** If the phone's mobile hotspot (tethering) is active, Wi-Fi Direct is often disabled at the driver level because they share the same soft-AP hardware interface.
*   **Background Throttling:** Android 13+ heavily throttles Wi-Fi scans. If your app is not in the foreground (or running a Foreground Service), the OS will rate-limit or drop your scan requests.

## 3. The "Empty List" Optimization
If the Wi-Fi hardware successfully scans but finds exactly **0 peers**, and the previous state was also **0 peers**, the Android framework may not broadcast `WIFI_P2P_PEERS_CHANGED_ACTION` because the state hasn't technically "changed". 
*   **Note:** Smart TVs and printers in a dorm might be on the infrastructure Wi-Fi, but they might not be emitting Wi-Fi Direct beacons unless explicitly put into a "Screen Mirroring" or "P2P Pairing" mode.

---

## 4. Diagnostic Code Modifications (`WiFiDirectManager.kt`)

To force the OS to reveal its state, you need to expand your `IntentFilter` and `BroadcastReceiver` to log **every** Wi-Fi Direct lifecycle event. 

Add the following exhaustive diagnostic receiver to your manager:

```kotlin
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.wifi.p2p.WifiP2pManager
import android.util.Log

class WiFiDirectDiagnosticReceiver(
    private val manager: WifiP2pManager,
    private val channel: WifiP2pManager.Channel
) : BroadcastReceiver() {

    companion object {
        const val TAG = "WiFiDirectDiag"

        // Call this when registering your receiver
        fun getIntentFilter(): IntentFilter {
            return IntentFilter().apply {
                addAction(WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION)
                addAction(WifiP2pManager.WIFI_P2P_DISCOVERY_CHANGED_ACTION)
                addAction(WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION)
                addAction(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION)
                addAction(WifiP2pManager.WIFI_P2P_THIS_DEVICE_CHANGED_ACTION)
            }
        }
    }

    override fun onReceive(context: Context, intent: Intent) {
        when (intent.action) {
            WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION -> {
                val state = intent.getIntExtra(WifiP2pManager.EXTRA_WIFI_STATE, -1)
                val isEnabled = state == WifiP2pManager.WIFI_P2P_STATE_ENABLED
                Log.d(TAG, "STATE_CHANGED: Wi-Fi P2P is ${if (isEnabled) "ENABLED" else "DISABLED"}")
            }

            WifiP2pManager.WIFI_P2P_DISCOVERY_CHANGED_ACTION -> {
                val state = intent.getIntExtra(WifiP2pManager.EXTRA_DISCOVERY_STATE, -1)
                if (state == WifiP2pManager.WIFI_P2P_DISCOVERY_STARTED) {
                    Log.d(TAG, "DISCOVERY_CHANGED: Hardware scan ACTUALLY STARTED.")
                } else if (state == WifiP2pManager.WIFI_P2P_DISCOVERY_STOPPED) {
                    Log.d(TAG, "DISCOVERY_CHANGED: Hardware scan STOPPED.")
                }
            }

            WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION -> {
                Log.d(TAG, "PEERS_CHANGED: OS reported a change in peer list. Requesting latest list...")
                
                // You must manually request the peers to see them
                try {
                    manager.requestPeers(channel) { peers ->
                        Log.d(TAG, "PEER LIST SIZE: ${peers.deviceList.size}")
                        peers.deviceList.forEach { device ->
                            Log.d(TAG, "Found Peer: ${device.deviceName} (${device.deviceAddress})")
                        }
                    }
                } catch (e: SecurityException) {
                    Log.e(TAG, "SecurityException requesting peers (Missing permissions)", e)
                }
            }

            WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION -> {
                Log.d(TAG, "CONNECTION_CHANGED: Network info updated.")
            }

            WifiP2pManager.WIFI_P2P_THIS_DEVICE_CHANGED_ACTION -> {
                Log.d(TAG, "THIS_DEVICE_CHANGED: Local device config updated.")
            }
            
            else -> {
                Log.w(TAG, "Received unknown intent: ${intent.action}")
            }
        }
    }
}
```

### How to use this for debugging:
1. Register this receiver in your Activity or Fragment:
   ```kotlin
   val diagReceiver = WiFiDirectDiagnosticReceiver(manager, channel)
   registerReceiver(diagReceiver, WiFiDirectDiagnosticReceiver.getIntentFilter())
   ```
2. Call `manager.discoverPeers(...)`.
3. **Watch Logcat:**
   - If you see `DISCOVERY_CHANGED: Hardware scan ACTUALLY STARTED.` followed immediately by `STOPPED` without a `PEERS_CHANGED`, the Wi-Fi hardware aborted the scan (likely due to DFS/Hotspot conflict).
   - If you don't even see `DISCOVERY_CHANGED`, your hardware is flat-out rejecting the P2P request despite `onSuccess`. Disconnect from the college Wi-Fi entirely and try again.
