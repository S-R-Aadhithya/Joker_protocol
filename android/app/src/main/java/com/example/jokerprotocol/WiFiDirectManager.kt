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
    private val onConnectionChanged: (Boolean, String?) -> Unit,
    private val onDiscoveryStateChanged: (Boolean, String?) -> Unit
) {
    private val manager: WifiP2pManager? = context.getSystemService(Context.WIFI_P2P_SERVICE) as? WifiP2pManager
    private val channel: WifiP2pManager.Channel? = manager?.initialize(context, Looper.getMainLooper(), null)
    
    private val intentFilter = IntentFilter().apply {
        addAction(WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION)
        addAction(WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION)
        addAction(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION)
        addAction(WifiP2pManager.WIFI_P2P_THIS_DEVICE_CHANGED_ACTION)
    }

    private val receiver = object : BroadcastReceiver() {
        @SuppressLint("MissingPermission")
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION -> {
                    manager?.requestPeers(channel) { peers: WifiP2pDeviceList ->
                        onPeersChanged(peers.deviceList.toList())
                        // Scanning finished when peers are updated
                        onDiscoveryStateChanged(false, null)
                    }
                }
                WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION -> {
                    val networkInfo = intent.getParcelableExtra<NetworkInfo>(WifiP2pManager.EXTRA_NETWORK_INFO)
                    if (networkInfo?.isConnected == true) {
                        manager?.requestConnectionInfo(channel) { info: WifiP2pInfo ->
                            val groupOwnerIp = info.groupOwnerAddress?.hostAddress
                            Log.d("WiFiDirect", "Connected. Group Owner IP: $groupOwnerIp")
                            onConnectionChanged(true, groupOwnerIp)
                        }
                    } else {
                        Log.d("WiFiDirect", "Disconnected.")
                        onConnectionChanged(false, null)
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

    @SuppressLint("MissingPermission")
    fun discoverPeers() {
        onDiscoveryStateChanged(true, null)
        manager?.discoverPeers(channel, object : WifiP2pManager.ActionListener {
            override fun onSuccess() {
                Log.d("WiFiDirect", "Peer discovery started")
                // Success means scanning started, keep UI loading until peers found
            }
            override fun onFailure(reasonCode: Int) {
                Log.e("WiFiDirect", "Peer discovery failed: $reasonCode")
                val errorMsg = when (reasonCode) {
                    WifiP2pManager.P2P_UNSUPPORTED -> "Wi-Fi Direct is not supported on this device."
                    WifiP2pManager.ERROR -> "Internal error. Make sure Wi-Fi is turned on in settings!"
                    WifiP2pManager.BUSY -> "Wi-Fi chip is busy. Please try again."
                    else -> "Unknown error occurred ($reasonCode)."
                }
                onDiscoveryStateChanged(false, errorMsg)
            }
        })
    }

    @SuppressLint("MissingPermission")
    fun connectToPeer(device: WifiP2pDevice) {
        val config = WifiP2pConfig().apply {
            deviceAddress = device.deviceAddress
        }
        manager?.connect(channel, config, object : WifiP2pManager.ActionListener {
            override fun onSuccess() {
                Log.d("WiFiDirect", "Connecting to ${device.deviceName}...")
            }
            override fun onFailure(reason: Int) {
                Log.e("WiFiDirect", "Connect failed: $reason")
            }
        })
    }
}
