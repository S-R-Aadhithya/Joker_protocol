package com.example.jokerprotocol

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Shader
import android.location.LocationManager
import android.net.Uri
import android.net.wifi.WifiManager
import android.net.wifi.p2p.WifiP2pDevice
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.OpenableColumns
import android.util.Base64
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusManager
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import androidx.lifecycle.lifecycleScope
import com.example.jokerprotocol.theme.JokerProtocolTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.io.ByteArrayOutputStream
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

sealed class ChatMessageItem {
    abstract val id: String
    abstract val sender: String
    abstract val isMine: Boolean
    abstract val timestamp: Long

    data class Text(
        override val id: String = UUID.randomUUID().toString(),
        override val sender: String,
        val text: String,
        override val isMine: Boolean,
        override val timestamp: Long = System.currentTimeMillis()
    ) : ChatMessageItem()

    data class Media(
        override val id: String = UUID.randomUUID().toString(),
        override val sender: String,
        val fileName: String,
        val fileSize: Long,
        val mimeType: String,
        val filePath: String,
        override val isMine: Boolean,
        override val timestamp: Long = System.currentTimeMillis()
    ) : ChatMessageItem()
}

data class ActiveTransfer(
    val transferId: String,
    val fileName: String,
    val currentChunk: Int,
    val totalChunks: Int,
    val isSending: Boolean
) {
    val progress: Float get() = if (totalChunks > 0) currentChunk.toFloat() / totalChunks else 0f
    val percentage: Int get() = (progress * 100).toInt().coerceIn(0, 100)
}

data class IncomingTransfer(
    val transferId: String,
    val fileName: String,
    val fileSize: Long,
    val totalChunks: Int,
    val mimeType: String,
    val chunks: ConcurrentHashMap<Int, ByteArray> = ConcurrentHashMap()
)

class MainActivity : ComponentActivity() {
    
    companion object {
        private val _nativeLogs = MutableStateFlow<List<String>>(emptyList())
        val nativeLogs = _nativeLogs.asStateFlow()

        private val _chatItems = MutableStateFlow<List<ChatMessageItem>>(emptyList())
        val chatItems = _chatItems.asStateFlow()

        private val _activeTransfer = MutableStateFlow<ActiveTransfer?>(null)
        val activeTransfer = _activeTransfer.asStateFlow()

        private val incomingTransfers = ConcurrentHashMap<String, IncomingTransfer>()
        private var appContext: Context? = null

        @JvmStatic
        fun onNativeLog(message: String) {
            _nativeLogs.update { logs ->
                if (logs.size >= 300) {
                    logs.drop(logs.size - 299) + message
                } else {
                    logs + message
                }
            }
        }

        @JvmStatic
        fun onPacketReceived(payload: String) {
            try {
                if (payload.startsWith("FT_META:")) {
                    // FT_META:<transferId>:<fileName>:<fileSize>:<totalChunks>:<mimeType>
                    val parts = payload.split(":", limit = 6)
                    if (parts.size >= 6) {
                        val transferId = parts[1]
                        val fileName = parts[2]
                        val fileSize = parts[3].toLongOrNull() ?: 0L
                        val totalChunks = parts[4].toIntOrNull() ?: 1
                        val mimeType = parts[5]
                        incomingTransfers[transferId] = IncomingTransfer(transferId, fileName, fileSize, totalChunks, mimeType)
                        _activeTransfer.value = ActiveTransfer(transferId, fileName, 0, totalChunks, false)
                    }
                } else if (payload.startsWith("FT_CHUNK:")) {
                    // FT_CHUNK:<transferId>:<chunkIndex>:<base64Data>
                    val parts = payload.split(":", limit = 4)
                    if (parts.size >= 4) {
                        val transferId = parts[1]
                        val chunkIndex = parts[2].toIntOrNull() ?: 0
                        val base64Data = parts[3]
                        val transfer = incomingTransfers[transferId]
                        if (transfer != null) {
                            val chunkBytes = Base64.decode(base64Data, Base64.NO_WRAP)
                            transfer.chunks[chunkIndex] = chunkBytes
                            _activeTransfer.value = ActiveTransfer(
                                transferId, transfer.fileName, transfer.chunks.size, transfer.totalChunks, false
                            )
                        }
                    }
                } else if (payload.startsWith("FT_DONE:")) {
                    // FT_DONE:<transferId>
                    val transferId = payload.substringAfter("FT_DONE:").trim()
                    val transfer = incomingTransfers.remove(transferId)
                    _activeTransfer.value = null
                    if (transfer != null && appContext != null) {
                        val downloadDir = appContext!!.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS)
                        val destFile = File(downloadDir, transfer.fileName)
                        destFile.outputStream().use { out ->
                            for (i in 0 until transfer.totalChunks) {
                                val chunk = transfer.chunks[i] ?: ByteArray(0)
                                out.write(chunk)
                            }
                        }
                        _chatItems.update { current ->
                            current + ChatMessageItem.Media(
                                sender = "Peer",
                                fileName = transfer.fileName,
                                fileSize = transfer.fileSize,
                                mimeType = transfer.mimeType,
                                filePath = destFile.absolutePath,
                                isMine = false
                            )
                        }
                    }
                } else {
                    // Standard text message
                    _chatItems.update { current ->
                        current + ChatMessageItem.Text(
                            sender = "Peer",
                            text = payload,
                            isMine = false
                        )
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
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
    private val protocolStartedState = mutableStateOf(false)
    
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

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent?) {
        if (intent == null) return
        if (intent.getBooleanExtra("auto_start_dev", false)) {
            startJokerProtocol("0.0.0.0", true)
            protocolStartedState.value = true
        }
        val chatMsg = intent.getStringExtra("send_chat")
        if (!chatMsg.isNullOrBlank()) {
            if (!protocolStartedState.value) {
                startJokerProtocol("0.0.0.0", true)
                protocolStartedState.value = true
            }
            sendTextMessage(chatMsg)
        }
        if (intent.getBooleanExtra("send_sample_file", false)) {
            if (!protocolStartedState.value) {
                startJokerProtocol("0.0.0.0", true)
                protocolStartedState.value = true
            }
            val demoBytes = createDemoImage()
            sendMedia("joker_demo_${System.currentTimeMillis() % 10000}.jpg", demoBytes, "image/jpeg")
        }
        if (intent.getBooleanExtra("auto_discover", false)) {
            checkPermissionsAndDiscover()
        }
        if (intent.getBooleanExtra("stop_protocol", false)) {
            stopJokerProtocol()
            protocolStartedState.value = false
        }
    }

    fun sendTextMessage(msg: String) {
        sendChatMessage(msg)
        _chatItems.update { current ->
            current + ChatMessageItem.Text(
                sender = "Me",
                text = msg,
                isMine = true
            )
        }
    }

    fun sendMedia(fileName: String, data: ByteArray, mimeType: String) {
        lifecycleScope.launch(Dispatchers.IO) {
            val transferId = UUID.randomUUID().toString().take(8)
            val chunkSize = 800
            val totalChunks = (data.size + chunkSize - 1) / chunkSize
            
            _activeTransfer.value = ActiveTransfer(transferId, fileName, 0, totalChunks, true)
            
            // Save local copy so sender can preview
            val downloadDir = getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS)
            val localFile = File(downloadDir, "sent_$fileName")
            localFile.writeBytes(data)

            // 1. Send metadata
            sendChatMessage("FT_META:$transferId:$fileName:${data.size}:$totalChunks:$mimeType")
            delay(20)

            // 2. Send chunks with pacing
            for (i in 0 until totalChunks) {
                val start = i * chunkSize
                val end = kotlin.math.min(data.size, (i + 1) * chunkSize)
                val chunk = data.copyOfRange(start, end)
                val b64 = Base64.encodeToString(chunk, Base64.NO_WRAP)
                sendChatMessage("FT_CHUNK:$transferId:$i:$b64")
                _activeTransfer.value = ActiveTransfer(transferId, fileName, i + 1, totalChunks, true)
                delay(8)
            }

            // 3. Send done
            delay(10)
            sendChatMessage("FT_DONE:$transferId")
            _activeTransfer.value = null

            _chatItems.update { current ->
                current + ChatMessageItem.Media(
                    sender = "Me",
                    fileName = fileName,
                    fileSize = data.size.toLong(),
                    mimeType = mimeType,
                    filePath = localFile.absolutePath,
                    isMine = true
                )
            }
        }
    }

    fun createDemoImage(): ByteArray {
        val width = 450
        val height = 300
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        
        val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        val gradient = LinearGradient(
            0f, 0f, width.toFloat(), height.toFloat(),
            Color.rgb(30, 60, 114),
            Color.rgb(42, 187, 155),
            Shader.TileMode.CLAMP
        )
        paint.shader = gradient
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)
        paint.shader = null
        
        paint.color = Color.argb(40, 255, 255, 255)
        canvas.drawRoundRect(20f, 20f, (width - 20).toFloat(), (height - 20).toFloat(), 20f, 20f, paint)

        paint.color = Color.WHITE
        paint.textSize = 28f
        paint.isFakeBoldText = true
        canvas.drawText("JOKER PROTOCOL", 40f, 75f, paint)
        
        paint.textSize = 18f
        paint.isFakeBoldText = false
        paint.color = Color.rgb(220, 240, 255)
        canvas.drawText("Wi-Fi Direct P2P Media Transfer", 40f, 115f, paint)
        canvas.drawText("Opportunistic Wireless Routing", 40f, 145f, paint)

        val timeStr = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date())
        paint.textSize = 16f
        paint.color = Color.YELLOW
        canvas.drawText("Verified: $timeStr", 40f, 200f, paint)

        paint.color = Color.WHITE
        paint.textSize = 14f
        canvas.drawText("Zero Network / 100% Peer-to-Peer", 40f, 240f, paint)

        val stream = ByteArrayOutputStream()
        bitmap.compress(Bitmap.CompressFormat.JPEG, 85, stream)
        return stream.toByteArray()
    }

    fun createDemoDocument(): ByteArray {
        val text = """
            ==================================================
            JOKER PROTOCOL SPECIFICATION & TEST DOCUMENT
            ==================================================
            Paper: Opportunistic Routing with Delay Coordination
            Topology: Wi-Fi Direct Peer-to-Peer (Redmi 13 <-> OPPO Reno)
            MTU Payload: 1200 Bytes
            Chunk Slicing: 800 Bytes / Chunk
            Encoding: Base64 Framing
            Pacing: 8ms Inter-Frame Delay
            
            Status: Verified Over-the-Air Transmission.
            Generated At: ${Date()}
            ==================================================
        """.trimIndent()
        return text.toByteArray(Charsets.UTF_8)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appContext = applicationContext
        enableEdgeToEdge()
        
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        multicastLock = wifiManager.createMulticastLock("JokerMulticastLock")
        multicastLock?.setReferenceCounted(false)
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
        handleIntent(intent)

        setContent {
            JokerProtocolTheme { 
                val snackbarHostState = remember { SnackbarHostState() }
                val coroutineScope = rememberCoroutineScope()
                var showOnboarding by remember { mutableStateOf(false) }
                
                LaunchedEffect(errorMessageState.value) {
                    errorMessageState.value?.let { error ->
                        coroutineScope.launch {
                            snackbarHostState.showSnackbar(message = error)
                            errorMessageState.value = null
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
                            protocolStarted = protocolStartedState.value,
                            onDiscoverClick = { checkPermissionsAndDiscover() },
                            onConnectClick = { device -> wifiDirectManager.connectToPeer(device) },
                            onStartProtocolClick = { ip, isGo -> 
                                startJokerProtocol(ip, isGo) 
                                if (!isGo && groupOwnerIpState.value != null && groupOwnerIpState.value != "0.0.0.0") {
                                    setPeerIp(groupOwnerIpState.value!!)
                                }
                                protocolStartedState.value = true
                            },
                            onStopProtocolClick = { 
                                stopJokerProtocol()
                                wifiDirectManager.disconnect()
                                protocolStartedState.value = false
                            },
                            onSendChatClick = { msg -> sendTextMessage(msg) },
                            onSendMediaClick = { name, bytes, mime -> sendMedia(name, bytes, mime) },
                            onSendDemoImageClick = {
                                val bytes = createDemoImage()
                                sendMedia("joker_demo_${System.currentTimeMillis() % 10000}.jpg", bytes, "image/jpeg")
                            },
                            onSendDemoDocClick = {
                                val bytes = createDemoDocument()
                                sendMedia("joker_spec_${System.currentTimeMillis() % 10000}.txt", bytes, "text/plain")
                            },
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

fun formatFileSize(bytes: Long): String {
    return when {
        bytes >= 1024 * 1024 -> String.format(Locale.getDefault(), "%.1f MB", bytes / (1024.0 * 1024.0))
        bytes >= 1024 -> String.format(Locale.getDefault(), "%.1f KB", bytes / 1024.0)
        else -> "$bytes B"
    }
}

fun getFileNameFromUri(context: Context, uri: Uri): String? {
    var result: String? = null
    if (uri.scheme == "content") {
        val cursor = context.contentResolver.query(uri, null, null, null, null)
        cursor?.use {
            if (it.moveToFirst()) {
                val index = it.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (index != -1) {
                    result = it.getString(index)
                }
            }
        }
    }
    if (result == null) {
        result = uri.path?.substringAfterLast('/')
    }
    return result
}

fun openFileWithViewer(context: Context, filePath: String, mimeType: String) {
    try {
        val file = File(filePath)
        val uri = FileProvider.getUriForFile(
            context,
            "${context.packageName}.fileprovider",
            file
        )
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, mimeType)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        context.startActivity(intent)
    } catch (e: Exception) {
        Toast.makeText(context, "No app available to open this file", Toast.LENGTH_SHORT).show()
    }
}

@Composable
fun MainUi(
    peers: List<WifiP2pDevice>,
    isConnected: Boolean,
    goIp: String?,
    isGroupOwner: Boolean,
    isDiscovering: Boolean,
    protocolStarted: Boolean,
    onDiscoverClick: () -> Unit,
    onConnectClick: (WifiP2pDevice) -> Unit,
    onStartProtocolClick: (String, Boolean) -> Unit,
    onStopProtocolClick: () -> Unit,
    onSendChatClick: (String) -> Unit,
    onSendMediaClick: (String, ByteArray, String) -> Unit,
    onSendDemoImageClick: () -> Unit,
    onSendDemoDocClick: () -> Unit,
    focusManager: FocusManager
) {
    var chatMessage by remember { mutableStateOf("") }
    var selectedTab by remember { mutableIntStateOf(0) }
    var showAttachMenu by remember { mutableStateOf(false) }
    var previewImagePath by remember { mutableStateOf<String?>(null) }
    
    val logs by MainActivity.nativeLogs.collectAsState()
    val chatItems by MainActivity.chatItems.collectAsState()
    val activeTransfer by MainActivity.activeTransfer.collectAsState()
    val context = LocalContext.current

    val filePickerLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        if (uri != null) {
            val fileName = getFileNameFromUri(context, uri) ?: "file_${System.currentTimeMillis()}"
            val mimeType = context.contentResolver.getType(uri) ?: "application/octet-stream"
            val bytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() }
            if (bytes != null) {
                onSendMediaClick(fileName, bytes, mimeType)
            }
        }
    }

    BackHandler(enabled = protocolStarted) {
        onStopProtocolClick()
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
            modifier = Modifier.padding(bottom = 12.dp)
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
                    Spacer(modifier = Modifier.height(12.dp))
                    Button(
                        onClick = { 
                            val bindIp = if (isGroupOwner) (goIp ?: "0.0.0.0") else MainActivity.getLocalWifiDirectIp(goIp ?: "")
                            onStartProtocolClick(bindIp, isGroupOwner)
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

        Spacer(modifier = Modifier.height(12.dp))

        // Toggle between Discovery (Not Started) and Protocol UI (Started)
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
            
            Spacer(modifier = Modifier.height(12.dp))
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
            // PROTOCOL RUNNING: Tab Row + Modern P2P Chat/Media View
            
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(onClick = { onStopProtocolClick() }) {
                    Text("⬅️")
                }
                
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilterChip(
                        selected = selectedTab == 0,
                        onClick = { selectedTab = 0 },
                        label = { Text("💬 Chat & Media") }
                    )
                    FilterChip(
                        selected = selectedTab == 1,
                        onClick = { selectedTab = 1 },
                        label = { Text("📋 Logs (${logs.size})") }
                    )
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

            Spacer(modifier = Modifier.height(8.dp))

            // Active File Transfer Progress Banner
            if (activeTransfer != null) {
                val transfer = activeTransfer!!
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 8.dp),
                    shape = RoundedCornerShape(12.dp),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.tertiaryContainer)
                ) {
                    Column(modifier = Modifier.padding(12.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                text = if (transfer.isSending) "📤 Sending: " else "📥 Receiving: ",
                                fontWeight = FontWeight.Bold,
                                style = MaterialTheme.typography.bodyMedium
                            )
                            Text(
                                text = transfer.fileName,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.weight(1f),
                                style = MaterialTheme.typography.bodyMedium
                            )
                            Text(
                                text = "${transfer.percentage}%",
                                fontWeight = FontWeight.Bold,
                                style = MaterialTheme.typography.bodyMedium
                            )
                        }
                        Spacer(modifier = Modifier.height(6.dp))
                        LinearProgressIndicator(
                            progress = { transfer.progress },
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(8.dp)
                                .clip(RoundedCornerShape(4.dp))
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = "${transfer.currentChunk} of ${transfer.totalChunks} chunks",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onTertiaryContainer
                        )
                    }
                }
            }

            if (selectedTab == 0) {
                // TAB 0: Interactive P2P Chat & Media Feed
                val listState = rememberLazyListState()
                
                LaunchedEffect(chatItems.size) {
                    if (chatItems.isNotEmpty()) {
                        listState.animateScrollToItem(chatItems.size - 1)
                    }
                }

                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth()
                ) {
                    if (chatItems.isEmpty()) {
                        Column(
                            modifier = Modifier
                                .fillMaxSize()
                                .padding(24.dp),
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.Center
                        ) {
                            Text("⚡", fontSize = 42.sp)
                            Spacer(modifier = Modifier.height(8.dp))
                            Text(
                                "JOKER P2P Network Live",
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold
                            )
                            Spacer(modifier = Modifier.height(4.dp))
                            Text(
                                "Send text messages, demo photos, or documents directly over Wi-Fi Direct without internet.",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                textAlign = androidx.compose.ui.text.style.TextAlign.Center
                            )
                        }
                    } else {
                        LazyColumn(
                            state = listState,
                            modifier = Modifier.fillMaxSize(),
                            contentPadding = PaddingValues(vertical = 8.dp)
                        ) {
                            items(chatItems, key = { it.id }) { item ->
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(vertical = 4.dp),
                                    horizontalArrangement = if (item.isMine) Arrangement.End else Arrangement.Start
                                ) {
                                    Card(
                                        shape = RoundedCornerShape(
                                            topStart = 16.dp,
                                            topEnd = 16.dp,
                                            bottomStart = if (item.isMine) 16.dp else 4.dp,
                                            bottomEnd = if (item.isMine) 4.dp else 16.dp
                                        ),
                                        colors = CardDefaults.cardColors(
                                            containerColor = if (item.isMine) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceVariant
                                        ),
                                        modifier = Modifier.widthIn(max = 280.dp)
                                    ) {
                                        Column(modifier = Modifier.padding(10.dp)) {
                                            Text(
                                                text = if (item.isMine) "You" else item.sender,
                                                style = MaterialTheme.typography.labelSmall,
                                                fontWeight = FontWeight.Bold,
                                                color = if (item.isMine) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary
                                            )
                                            Spacer(modifier = Modifier.height(2.dp))
                                            
                                            when (item) {
                                                is ChatMessageItem.Text -> {
                                                    Text(
                                                        text = item.text,
                                                        style = MaterialTheme.typography.bodyMedium
                                                    )
                                                }
                                                is ChatMessageItem.Media -> {
                                                    val isImage = item.mimeType.startsWith("image") || 
                                                                  item.fileName.endsWith(".jpg", true) || 
                                                                  item.fileName.endsWith(".png", true) ||
                                                                  item.fileName.endsWith(".jpeg", true)
                                                    
                                                    if (isImage) {
                                                        val bitmap = remember(item.filePath) {
                                                            try {
                                                                BitmapFactory.decodeFile(item.filePath)
                                                            } catch (e: Exception) {
                                                                null
                                                            }
                                                        }
                                                        if (bitmap != null) {
                                                            Image(
                                                                bitmap = bitmap.asImageBitmap(),
                                                                contentDescription = item.fileName,
                                                                modifier = Modifier
                                                                    .fillMaxWidth()
                                                                    .heightIn(max = 180.dp)
                                                                    .clip(RoundedCornerShape(8.dp))
                                                                    .clickable { previewImagePath = item.filePath }
                                                            )
                                                            Spacer(modifier = Modifier.height(4.dp))
                                                        }
                                                    } else {
                                                        Row(
                                                            verticalAlignment = Alignment.CenterVertically,
                                                            modifier = Modifier.padding(vertical = 4.dp)
                                                        ) {
                                                            Text("📄", fontSize = 28.sp)
                                                            Spacer(modifier = Modifier.width(8.dp))
                                                            Column(modifier = Modifier.weight(1f)) {
                                                                Text(
                                                                    text = item.fileName,
                                                                    fontWeight = FontWeight.SemiBold,
                                                                    maxLines = 1,
                                                                    overflow = TextOverflow.Ellipsis
                                                                )
                                                                Text(
                                                                    text = formatFileSize(item.fileSize),
                                                                    style = MaterialTheme.typography.labelSmall
                                                                )
                                                            }
                                                        }
                                                        Button(
                                                            onClick = { openFileWithViewer(context, item.filePath, item.mimeType) },
                                                            modifier = Modifier.fillMaxWidth().height(36.dp),
                                                            shape = RoundedCornerShape(8.dp)
                                                        ) {
                                                            Text("Open File", fontSize = 12.sp)
                                                        }
                                                        Spacer(modifier = Modifier.height(4.dp))
                                                    }
                                                    
                                                    Row(
                                                        modifier = Modifier.fillMaxWidth(),
                                                        horizontalArrangement = Arrangement.SpaceBetween
                                                    ) {
                                                        Text(
                                                            text = item.fileName,
                                                            style = MaterialTheme.typography.labelSmall,
                                                            maxLines = 1,
                                                            overflow = TextOverflow.Ellipsis,
                                                            modifier = Modifier.weight(1f)
                                                        )
                                                        Text(
                                                            text = formatFileSize(item.fileSize),
                                                            style = MaterialTheme.typography.labelSmall
                                                        )
                                                    }
                                                }
                                            }
                                            
                                            Text(
                                                text = SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(item.timestamp)),
                                                style = MaterialTheme.typography.labelSmall,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                                                modifier = Modifier.align(Alignment.End)
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Chat Input Row with Attachment Menu
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Box {
                        IconButton(onClick = { showAttachMenu = true }) {
                            Text("📎", fontSize = 22.sp)
                        }
                        DropdownMenu(
                            expanded = showAttachMenu,
                            onDismissRequest = { showAttachMenu = false }
                        ) {
                            DropdownMenuItem(
                                text = { Text("🖼️ Choose Photo / File") },
                                onClick = {
                                    showAttachMenu = false
                                    filePickerLauncher.launch("*/*")
                                }
                            )
                            DropdownMenuItem(
                                text = { Text("🎨 Send Demo Photo (1-Tap)") },
                                onClick = {
                                    showAttachMenu = false
                                    onSendDemoImageClick()
                                }
                            )
                            DropdownMenuItem(
                                text = { Text("📄 Send Demo Spec Doc (1-Tap)") },
                                onClick = {
                                    showAttachMenu = false
                                    onSendDemoDocClick()
                                }
                            )
                        }
                    }

                    OutlinedTextField(
                        value = chatMessage,
                        onValueChange = { chatMessage = it },
                        modifier = Modifier.weight(1f),
                        placeholder = { Text("Message...") },
                        shape = RoundedCornerShape(24.dp),
                        singleLine = true
                    )
                    Spacer(modifier = Modifier.width(6.dp))
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
            } else {
                // TAB 1: Live Protocol Log Monitor
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
            }
        }
    }

    // Fullscreen Image Preview Dialog
    if (previewImagePath != null) {
        Dialog(onDismissRequest = { previewImagePath = null }) {
            Card(
                shape = RoundedCornerShape(16.dp),
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(8.dp)
            ) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    val bitmap = remember(previewImagePath) {
                        try {
                            BitmapFactory.decodeFile(previewImagePath)
                        } catch (e: Exception) {
                            null
                        }
                    }
                    if (bitmap != null) {
                        Image(
                            bitmap = bitmap.asImageBitmap(),
                            contentDescription = "Preview",
                            modifier = Modifier
                                .fillMaxWidth()
                                .heightIn(max = 400.dp)
                                .clip(RoundedCornerShape(8.dp))
                        )
                    } else {
                        Text("Unable to load preview")
                    }
                    Spacer(modifier = Modifier.height(12.dp))
                    Button(onClick = { previewImagePath = null }) {
                        Text("Close")
                    }
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
                    text = "How to connect with Wi-Fi Direct",
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold
                )
                Spacer(modifier = Modifier.height(16.dp))
                Text(
                    text = "1. Ensure Wi-Fi is turned ON on both devices (disconnect from regular routers if needed).\n\n" +
                           "2. Click 'Discover Peers' to search for nearby phones.\n\n" +
                           "3. Tap on the peer's name to invite and accept the connection on the other phone.\n\n" +
                           "4. Once connected, click 'Start JOKER Protocol' on both devices to start opportunistic routing, chat, and media transfer!",
                    style = MaterialTheme.typography.bodyMedium
                )
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
