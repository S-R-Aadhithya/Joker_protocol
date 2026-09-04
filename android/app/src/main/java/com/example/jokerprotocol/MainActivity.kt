package com.example.jokerprotocol

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.pm.PackageManager
import android.location.LocationManager
import android.net.wifi.WifiManager
import android.net.wifi.p2p.WifiP2pDevice
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.compose.BackHandler
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusManager
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.core.content.ContextCompat
import com.example.jokerprotocol.theme.JokerProtocolTheme
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    
    companion object {
        private val _nativeLogs = MutableStateFlow<List<String>>(emptyList())
        val nativeLogs = _nativeLogs.asStateFlow()

        @JvmStatic
        fun onNativeLog(message: String) {
            _nativeLogs.update { it + message }
        }

        fun getLocalWifiDirectIp(groupOwnerIp: String): String {
            try {
                if (groupOwnerIp.isBlank() || groupOwnerIp == "0.0.0.0") return "0.0.0.0"
                val prefix = groupOwnerIp.substringBeforeLast(".") + "."
                val interfaces = java.net.NetworkInterface.getNetworkInterfaces()
                for (intf in interfaces) {
                    for (addr in intf.inetAddresses) {
                        if (!addr.isLoopbackAddress && addr is java.net.Inet4Address) {
                            val ip = addr.hostAddress
                            if (ip?.startsWith(prefix) == true) {
                                return ip
                            }
                        }
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
            return "0.0.0.0"
        }
    }

    init {
        System.loadLibrary("joker_jni")
    }

    external fun startJokerProtocol(bindIp: String, isGroupOwner: Boolean)
    external fun stopJokerProtocol()
    external fun sendChatMessage(message: String)
    external fun setPeerIp(ip: String)

    private lateinit var wifiDirectManager: WiFiDirectManager
    private var multicastLock: WifiManager.MulticastLock? = null
    
    // UI States
    private val peersState = mutableStateOf<List<WifiP2pDevice>>(emptyList())
    private val connectionState = mutableStateOf(false)
    private val groupOwnerIpState = mutableStateOf<String?>("0.0.0.0")
    private val isGroupOwnerState = mutableStateOf(false)
    
    // New UI States for Discovery
    private val isDiscoveringState = mutableStateOf(false)
    private val errorMessageState = mutableStateOf<String?>(null)

    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        if (permissions.all { it.value }) {
            wifiDirectManager.discoverPeers()
        } else {
            errorMessageState.value = "Permissions are required to scan for peers."
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        multicastLock = wifiManager.createMulticastLock("JokerMulticastLock")
        multicastLock?.acquire()

        wifiDirectManager = WiFiDirectManager(
            context = this,
            onPeersChanged = { peers ->
                peersState.value = peers
            },
            onConnectionChanged = { isConnected, ip, isGo ->
                connectionState.value = isConnected
                groupOwnerIpState.value = ip ?: "0.0.0.0"
                isGroupOwnerState.value = isGo
            },
            onDiscoveryStateChanged = { isDiscovering, error ->
                isDiscoveringState.value = isDiscovering
                if (error != null) {
                    errorMessageState.value = error
                }
            }
        )
        
        wifiDirectManager.startListening()

        setContent {
            JokerProtocolTheme { 
                val snackbarHostState = remember { SnackbarHostState() }
                val coroutineScope = rememberCoroutineScope()
                var showOnboarding by remember { mutableStateOf(true) } // Show on first launch
                
                // Show errors as snackbars automatically
                LaunchedEffect(errorMessageState.value) {
                    errorMessageState.value?.let { error ->
                        coroutineScope.launch {
                            snackbarHostState.showSnackbar(message = error)
                            errorMessageState.value = null // clear after showing
                        }
                    }
                }

                Scaffold(
                    snackbarHost = { SnackbarHost(snackbarHostState) },
                    topBar = {
                        @OptIn(ExperimentalMaterial3Api::class)
                        TopAppBar(
                            title = { Text("JOKER Protocol", fontWeight = FontWeight.Bold) },
                            actions = {
                                TextButton(onClick = { showOnboarding = true }) {
                                    Text("Help / Manual")
                                }
                            }
                        )
                    }
                ) { paddingValues ->
                    val focusManager = LocalFocusManager.current
                    
                    Box(modifier = Modifier.padding(paddingValues)) {
                        MainUi(
                            peers = peersState.value,
                            isConnected = connectionState.value,
                            goIp = groupOwnerIpState.value,
                            isGroupOwner = isGroupOwnerState.value,
                            isDiscovering = isDiscoveringState.value,
                            onDiscoverClick = { checkPermissionsAndDiscover() },
                            onConnectClick = { device -> wifiDirectManager.connectToPeer(device) },
                            onStartProtocolClick = { ip, isGo -> 
                                startJokerProtocol(ip, isGo) 
                                if (!isGo && groupOwnerIpState.value != null && groupOwnerIpState.value != "0.0.0.0") {
                                    setPeerIp(groupOwnerIpState.value!!)
                                }
                            },
                            onStopProtocolClick = { 
                                stopJokerProtocol()
                                wifiDirectManager.disconnect()
                            },
                            onSendChatClick = { msg -> sendChatMessage(msg) },
                            focusManager = focusManager
                        )

                        if (showOnboarding) {
                            OnboardingScreen(onDismiss = { showOnboarding = false })
                        }
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        wifiDirectManager.disconnect()
        wifiDirectManager.stopListening()
        stopJokerProtocol()
        multicastLock?.release()
    }

    private fun checkPermissionsAndDiscover() {
        // First check if Location Services (GPS) are globally enabled
        val locationManager = getSystemService(Context.LOCATION_SERVICE) as LocationManager
        val isGpsEnabled = locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)
        val isNetworkEnabled = locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)
        
        if (!isGpsEnabled && !isNetworkEnabled) {
            errorMessageState.value = "Location Services must be turned ON in your phone's quick settings to discover Wi-Fi Direct peers."
            return
        }

        val permissions = mutableListOf(
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.ACCESS_COARSE_LOCATION
        )
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions.add(Manifest.permission.NEARBY_WIFI_DEVICES)
        }

        val missing = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }

        if (missing.isEmpty()) {
            wifiDirectManager.discoverPeers()
        } else {
            requestPermissionLauncher.launch(missing.toTypedArray())
        }
    }
}

@Composable
fun MainUi(
    peers: List<WifiP2pDevice>,
    isConnected: Boolean,
    goIp: String?,
    isGroupOwner: Boolean,
    isDiscovering: Boolean,
    onDiscoverClick: () -> Unit,
    onConnectClick: (WifiP2pDevice) -> Unit,
    onStartProtocolClick: (String, Boolean) -> Unit,
    onStopProtocolClick: () -> Unit,
    onSendChatClick: (String) -> Unit,
    focusManager: FocusManager
) {
    var chatMessage by remember { mutableStateOf("") }
    var protocolStarted by remember { mutableStateOf(false) }
    
    val logs by MainActivity.nativeLogs.collectAsState()
    val context = LocalContext.current

    BackHandler(enabled = protocolStarted) {
        onStopProtocolClick()
        protocolStarted = false
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .pointerInput(Unit) {
                detectTapGestures(onTap = { focusManager.clearFocus() })
            }
            .padding(16.dp)
    ) {
        // App Title and Version
        Text(
            text = "JOKER Protocol v3.0",
            style = MaterialTheme.typography.headlineSmall,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.padding(bottom = 16.dp)
        )

        // Status Card with animation
        Card(
            modifier = Modifier
                .fillMaxWidth()
                .animateContentSize(),
            shape = RoundedCornerShape(16.dp),
            elevation = CardDefaults.cardElevation(defaultElevation = 4.dp),
            colors = CardDefaults.cardColors(
                containerColor = if (isConnected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceVariant
            )
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = if (isConnected) "●" else "○",
                        color = if (isConnected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                        style = MaterialTheme.typography.titleLarge
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = if (isConnected) "Wi-Fi Direct Connected" else "Disconnected",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                }
                
                if (isConnected) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text("Group Owner IP: $goIp", style = MaterialTheme.typography.bodyMedium)
                    Spacer(modifier = Modifier.height(16.dp))
                    Button(
                        onClick = { 
                            val bindIp = if (isGroupOwner) (goIp ?: "0.0.0.0") else MainActivity.getLocalWifiDirectIp(goIp ?: "")
                            onStartProtocolClick(bindIp, isGroupOwner)
                            protocolStarted = true 
                        },
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(8.dp),
                        enabled = !protocolStarted
                    ) {
                        Text(if (protocolStarted) "Protocol Running" else "Start JOKER Protocol")
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Toggle between Discovery (Not Started) and Chat UI (Started)
        if (!protocolStarted) {
            Button(
                onClick = onDiscoverClick, 
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(8.dp),
                enabled = !isDiscovering
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(if (isDiscovering) "Scanning for Peers..." else "Discover Peers")
                }
            }
            
            if (isDiscovering) {
                LinearProgressIndicator(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
                    color = MaterialTheme.colorScheme.primary
                )
                Text(
                    "Searching the area for Wi-Fi Direct devices...",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.align(Alignment.CenterHorizontally)
                )
            }
            
            Spacer(modifier = Modifier.height(8.dp))
            Button(
                onClick = { 
                    onStartProtocolClick("0.0.0.0", true)
                    protocolStarted = true 
                },
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(8.dp),
                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.tertiary)
            ) {
                Text("Force Start JOKER (Dev Mode)")
            }

            Spacer(modifier = Modifier.height(16.dp))
            Text("Available Peers", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            
            LazyColumn(
                modifier = Modifier.weight(1f),
                contentPadding = PaddingValues(bottom = 16.dp)
            ) {
                if (peers.isEmpty() && !isDiscovering) {
                    item {
                        Text(
                            text = "No peers found. Ensure Wi-Fi is on and click discover.",
                            modifier = Modifier.padding(8.dp),
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                items(peers) { peer ->
                    Card(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 4.dp)
                            .clickable { onConnectClick(peer) },
                        shape = RoundedCornerShape(12.dp),
                        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
                    ) {
                        Column(modifier = Modifier.padding(16.dp)) {
                            Text(peer.deviceName, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.Medium)
                            Text(peer.deviceAddress, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("Discovery Logs", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
                IconButton(onClick = {
                    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                    val clip = ClipData.newPlainText("Joker Logs", logs.joinToString("\n"))
                    clipboard.setPrimaryClip(clip)
                    Toast.makeText(context, "Logs copied to clipboard!", Toast.LENGTH_SHORT).show()
                }) {
                    Text("📋")
                }
            }
            
            LazyColumn(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth(),
                reverseLayout = true,
                contentPadding = PaddingValues(bottom = 8.dp)
            ) {
                items(logs.reversed()) { log ->
                    Text(
                        text = log,
                        style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(vertical = 2.dp),
                        color = MaterialTheme.colorScheme.onBackground
                    )
                }
            }
        } else {
            // Chat UI for when the protocol is running
            
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    IconButton(onClick = {
                        onStopProtocolClick()
                        protocolStarted = false
                    }) {
                        Text("⬅️")
                    }
                    Text("Live Log Monitor", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
                }
                
                IconButton(onClick = {
                    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                    val clip = ClipData.newPlainText("Joker Logs", logs.joinToString("\n"))
                    clipboard.setPrimaryClip(clip)
                    Toast.makeText(context, "Logs copied to clipboard!", Toast.LENGTH_SHORT).show()
                }) {
                    Text("📋")
                }
            }
            
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
            ) {
                LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(bottom = 8.dp)
                ) {
                    items(logs) { log ->
                        Text(
                            text = log,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(vertical = 2.dp)
                        )
                    }
                }
            }
            
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                OutlinedTextField(
                    value = chatMessage,
                    onValueChange = { chatMessage = it },
                    modifier = Modifier.weight(1f),
                    placeholder = { Text("Enter message...") },
                    shape = RoundedCornerShape(24.dp),
                    singleLine = true
                )
                Spacer(modifier = Modifier.width(8.dp))
                Button(
                    onClick = {
                        if (chatMessage.isNotBlank()) {
                            onSendChatClick(chatMessage)
                            chatMessage = ""
                            focusManager.clearFocus()
                        }
                    },
                    modifier = Modifier.height(50.dp)
                ) {
                    Text("Send")
                }
            }
        }
    }
}

@Composable
fun OnboardingScreen(onDismiss: () -> Unit) {
    Dialog(onDismissRequest = onDismiss) {
        Card(
            shape = RoundedCornerShape(16.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(24.dp)) {
                Text(
                    text = "Welcome to JOKER",
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary
                )
                Spacer(modifier = Modifier.height(16.dp))
                Text(
                    text = "This app demonstrates the JOKER Protocol—a True P2P Opportunistic Routing mesh network over Wi-Fi Direct.",
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(modifier = Modifier.height(16.dp))
                Text("How to use:", fontWeight = FontWeight.Bold)
                Spacer(modifier = Modifier.height(8.dp))
                Text("1. Ensure Wi-Fi is toggled ON in Android Settings.")
                Text("2. Click 'Discover Peers' to scan the area.")
                Text("3. Tap a discovered phone to form a Wi-Fi Direct Group.")
                Text("4. Once Connected, tap 'Start JOKER Protocol' to launch the C++ routing engine.")
                Text("5. You can now chat peer-to-peer entirely off-grid!")
                
                Spacer(modifier = Modifier.height(24.dp))
                Button(
                    onClick = onDismiss,
                    modifier = Modifier.align(Alignment.End)
                ) {
                    Text("Got it")
                }
            }
        }
    }
}
