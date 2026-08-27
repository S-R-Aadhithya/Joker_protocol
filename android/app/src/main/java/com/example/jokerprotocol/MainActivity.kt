package com.example.jokerprotocol

import android.Manifest
import android.content.pm.PackageManager
import android.net.wifi.p2p.WifiP2pDevice
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
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
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.core.content.ContextCompat
import com.example.jokerprotocol.theme.JokerProtocolTheme
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    
    init {
        System.loadLibrary("joker_jni")
    }

    external fun startJokerProtocol(bindIp: String, isGroupOwner: Boolean)
    external fun stopJokerProtocol()
    external fun sendChatMessage(message: String)

    private lateinit var wifiDirectManager: WiFiDirectManager
    
    // UI States
    private val peersState = mutableStateOf<List<WifiP2pDevice>>(emptyList())
    private val connectionState = mutableStateOf(false)
    private val groupOwnerIpState = mutableStateOf<String?>("0.0.0.0")
    
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

        wifiDirectManager = WiFiDirectManager(
            context = this,
            onPeersChanged = { peers ->
                peersState.value = peers
            },
            onConnectionChanged = { isConnected, ip ->
                connectionState.value = isConnected
                groupOwnerIpState.value = ip ?: "0.0.0.0"
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
                            isDiscovering = isDiscoveringState.value,
                            onDiscoverClick = { checkPermissionsAndDiscover() },
                            onConnectClick = { device -> wifiDirectManager.connectToPeer(device) },
                            onStartProtocolClick = { ip -> startJokerProtocol(ip, true) },
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
        wifiDirectManager.stopListening()
        stopJokerProtocol()
    }

    private fun checkPermissionsAndDiscover() {
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
    isDiscovering: Boolean,
    onDiscoverClick: () -> Unit,
    onConnectClick: (WifiP2pDevice) -> Unit,
    onStartProtocolClick: (String) -> Unit,
    onSendChatClick: (String) -> Unit,
    focusManager: FocusManager
) {
    var chatMessage by remember { mutableStateOf("") }
    var protocolStarted by remember { mutableStateOf(false) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .pointerInput(Unit) {
                detectTapGestures(onTap = { focusManager.clearFocus() })
            }
            .padding(16.dp)
    ) {
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
                            onStartProtocolClick(goIp ?: "0.0.0.0")
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
                    if (isDiscovering) {
                        Spacer(modifier = Modifier.width(8.dp))
                        CircularProgressIndicator(
                            modifier = Modifier.size(16.dp), 
                            color = MaterialTheme.colorScheme.onPrimary,
                            strokeWidth = 2.dp
                        )
                    }
                }
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
        } else {
            // Chat UI for when the protocol is running
            Text("Network Chat", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
            
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
            ) {
                // Placeholder for future log/chat list
                Text(
                    text = "Ready to transmit payloads...",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.align(Alignment.Center)
                )
            }
            
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                OutlinedTextField(
                    value = chatMessage,
                    onValueChange = { chatMessage = it },
                    modifier = Modifier.weight(1f),
                    placeholder = { Text("Enter payload...") },
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
