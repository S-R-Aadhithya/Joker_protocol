package com.example.jokerprotocol

import android.annotation.SuppressLint
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.NetworkInfo
import android.net.wifi.p2p.WifiP2pConfig
import android.net.wifi.p2p.WifiP2pDevice
import android.net.wifi.p2p.WifiP2pDeviceList
import android.net.wifi.p2p.WifiP2pInfo
import android.net.wifi.p2p.WifiP2pManager
import android.os.Looper
import android.util.Log

class WiFiDirectManager(
    private val context: Context,
    private val onPeersChanged: (List<WifiP2pDevice>) -> Unit,
    private val onConnectionChanged: (Boolean, String?, Boolean) -> Unit,
    private val onDiscoveryStateChanged: (Boolean, String?) -> Unit
) {
    private val manager: WifiP2pManager? = context.getSystemService(Context.WIFI_P2P_SERVICE) as? WifiP2pManager
    private val channel: WifiP2pManager.Channel? = manager?.initialize(context, Looper.getMainLooper(), null)
    
    private val intentFilter = IntentFilter().apply {
        addAction(WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION)
        addAction(WifiP2pManager.WIFI_P2P_DISCOVERY_CHANGED_ACTION)
        addAction(WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION)
        addAction(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION)
        addAction(WifiP2pManager.WIFI_P2P_THIS_DEVICE_CHANGED_ACTION)
    }
    
    private var discoveryStartTime: Long = 0
    private var isConnecting: Boolean = false
    private val handler = android.os.Handler(Looper.getMainLooper())
    private val connectTimeoutRunnable = Runnable {
        if (isConnecting) {
            MainActivity.onNativeLog("[WiFi Direct] Connection attempt timed out. Canceling connection in HAL...")
            manager?.cancelConnect(channel, null)
            isConnecting = false
        }
    }
    private val receiver = object : BroadcastReceiver() {
        @SuppressLint("MissingPermission")
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION -> {
                    val state = intent.getIntExtra(WifiP2pManager.EXTRA_WIFI_STATE, -1)
                    val isEnabled = state == WifiP2pManager.WIFI_P2P_STATE_ENABLED
                    MainActivity.onNativeLog("[WiFi Direct] STATE_CHANGED: Wi-Fi P2P is ${if (isEnabled) "ENABLED" else "DISABLED"} at OS level.")
                }
                WifiP2pManager.WIFI_P2P_DISCOVERY_CHANGED_ACTION -> {
                    val state = intent.getIntExtra(WifiP2pManager.EXTRA_DISCOVERY_STATE, -1)
                    if (state == WifiP2pManager.WIFI_P2P_DISCOVERY_STARTED) {
                        MainActivity.onNativeLog("[WiFi Direct] DISCOVERY_CHANGED: Hardware scan ACTUALLY STARTED.")
                    } else if (state == WifiP2pManager.WIFI_P2P_DISCOVERY_STOPPED) {
                        MainActivity.onNativeLog("[WiFi Direct] DISCOVERY_CHANGED: Hardware scan STOPPED.")
                    }
                }
                WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION -> {
                    MainActivity.onNativeLog("[WiFi Direct] PEERS_CHANGED: OS reported a change in peer list. Requesting list...")
                    manager?.requestPeers(channel) { peers: WifiP2pDeviceList ->
                        onPeersChanged(peers.deviceList.toList())
                        
                        if (discoveryStartTime > 0) {
                            val duration = System.currentTimeMillis() - discoveryStartTime
                            MainActivity.onNativeLog("[WiFi Direct] Scan completed in ${duration}ms. Found ${peers.deviceList.size} peers.")
                            discoveryStartTime = 0
                        }
                        
                        // Scanning finished when peers are updated
                        onDiscoveryStateChanged(false, null)
                    }
                }
                WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION -> {
                    val networkInfo = intent.getParcelableExtra<NetworkInfo>(WifiP2pManager.EXTRA_NETWORK_INFO)
                    if (isConnecting) {
                        isConnecting = false
                        handler.removeCallbacks(connectTimeoutRunnable)
                    }
                    if (networkInfo?.isConnected == true) {
                        manager?.requestConnectionInfo(channel) { info: WifiP2pInfo ->
                            val groupOwnerIp = info.groupOwnerAddress?.hostAddress
                            MainActivity.onNativeLog("[WiFi Direct] Connected. Group Owner IP: $groupOwnerIp, isGroupOwner: ${info.isGroupOwner}")
                            onConnectionChanged(true, groupOwnerIp, info.isGroupOwner)
                        }
                    } else {
                        MainActivity.onNativeLog("[WiFi Direct] Disconnected.")
                        onConnectionChanged(false, null, false)
                    }
                }
            }
        }
    }

    fun startListening() {
        context.registerReceiver(receiver, intentFilter)
    }

    fun stopListening() {
        try {
            context.unregisterReceiver(receiver)
        } catch (e: IllegalArgumentException) {
            // Already unregistered
        }
    }

    fun disconnect() {
        MainActivity.onNativeLog("[WiFi Direct] Disconnecting and removing Wi-Fi Direct group...")
        manager?.removeGroup(channel, object : WifiP2pManager.ActionListener {
            override fun onSuccess() {
                MainActivity.onNativeLog("[WiFi Direct] Successfully removed group.")
            }
            override fun onFailure(reason: Int) {
                MainActivity.onNativeLog("[WiFi Direct] Failed to remove group. Code: $reason")
            }
        })
    }

    @SuppressLint("MissingPermission")
    private fun resetWiFiDirectState(onComplete: () -> Unit) {
        MainActivity.onNativeLog("[WiFi Direct] Attempting aggressive state reset to clear BUSY state...")

        // 1. Cancel any hung connections
        manager?.cancelConnect(channel, null)
        
        // 2. Remove any active groups (if we were a Group Owner)
        manager?.removeGroup(channel, null)
        
        // 3. Stop any stuck discovery
        manager?.stopPeerDiscovery(channel, null)

        // 4. Reflection: Nuke cached Persistent Groups (Hidden API)
        try {
            val deleteMethod = WifiP2pManager::class.java.getMethod(
                "deletePersistentGroup",
                WifiP2pManager.Channel::class.java,
                Int::class.javaPrimitiveType,
                WifiP2pManager.ActionListener::class.java
            )
            deleteMethod.isAccessible = true
            
            // Network IDs typically range from 0 to 32. We blindly delete them to clear the cache.
            for (netId in 0..31) {
                deleteMethod.invoke(manager, channel, netId, null)
            }
            MainActivity.onNativeLog("[WiFi Direct] Successfully invoked deletePersistentGroup via Reflection.")
        } catch (e: Exception) {
            MainActivity.onNativeLog("[WiFi Direct] Failed to invoke deletePersistentGroup: ${e.message}")
        }

        // Give the HAL a moment to process the teardown commands before proceeding
        android.os.Handler(Looper.getMainLooper()).postDelayed({
            onComplete()
        }, 500)
    }

    @SuppressLint("MissingPermission")
    fun discoverPeers(isRetry: Boolean = false) {
        onDiscoveryStateChanged(true, null)
        discoveryStartTime = System.currentTimeMillis()
        MainActivity.onNativeLog("[WiFi Direct] Initiating peer discovery scan...")
        manager?.discoverPeers(channel, object : WifiP2pManager.ActionListener {
            override fun onSuccess() {
                MainActivity.onNativeLog("[WiFi Direct] Discovery successfully started by OS.")
                // Success means scanning started, keep UI loading until peers found
            }
            override fun onFailure(reasonCode: Int) {
                if (reasonCode == WifiP2pManager.BUSY && !isRetry) {
                    MainActivity.onNativeLog("[WiFi Direct] Error BUSY (2) received. Initiating recovery sequence...")
                    resetWiFiDirectState {
                        MainActivity.onNativeLog("[WiFi Direct] Recovery sequence complete. Retrying discovery...")
                        discoverPeers(isRetry = true)
                    }
                    return
                }

                val errorMsg = when (reasonCode) {
                    WifiP2pManager.P2P_UNSUPPORTED -> "Wi-Fi Direct is not supported on this device."
                    WifiP2pManager.ERROR -> "Internal error. Make sure Wi-Fi is turned on in settings!"
                    WifiP2pManager.BUSY -> "Wi-Fi chip is permanently busy. Please toggle device Wi-Fi off and on."
                    else -> "Unknown error occurred ($reasonCode)."
                }
                MainActivity.onNativeLog("[WiFi Direct] Peer discovery failed: $errorMsg")
                onDiscoveryStateChanged(false, errorMsg)
            }
        })
    }

    @SuppressLint("MissingPermission")
    fun connectToPeer(device: WifiP2pDevice) {
        if (isConnecting) {
            MainActivity.onNativeLog("[WiFi Direct] Ignoring connection request to ${device.deviceName} (already connecting).")
            return
        }
        isConnecting = true
        handler.postDelayed(connectTimeoutRunnable, 60000)

        MainActivity.onNativeLog("[WiFi Direct] Stopping discovery before connecting to ${device.deviceName}...")
        
        manager?.stopPeerDiscovery(channel, object : WifiP2pManager.ActionListener {
            override fun onSuccess() {
                // Proceed with the new connection once the slate is clean
                android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                    executeConnect(device)
                }, 300)
            }
            override fun onFailure(reason: Int) {
                // Proceed anyway, as it might fail if there was nothing to cancel
                android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                    executeConnect(device)
                }, 300)
            }
        })
    }

    @SuppressLint("MissingPermission")
    private fun executeConnect(device: WifiP2pDevice) {
        val config = WifiP2pConfig().apply {
            deviceAddress = device.deviceAddress
        }
        MainActivity.onNativeLog("[WiFi Direct] Attempting to connect to ${device.deviceName}...")
        manager?.connect(channel, config, object : WifiP2pManager.ActionListener {
            override fun onSuccess() {
                MainActivity.onNativeLog("[WiFi Direct] Connection initiated to ${device.deviceName}. Waiting for negotiation...")
            }
            override fun onFailure(reason: Int) {
                isConnecting = false
                handler.removeCallbacks(connectTimeoutRunnable)
                MainActivity.onNativeLog("[WiFi Direct] Connect failed with code: $reason")
            }
        })
    }
}
