# 06 — Android Wi-Fi Direct & P2P Media Transfer

This document details how JOKER operates on modern Android devices over Wi-Fi Direct (P2P), the physical testbed verification between two real smartphones, and the architecture of the File & Media Streaming protocol.

---

## 1. Android Wi-Fi Direct (P2P) Architecture

Wi-Fi Direct (IEEE 802.11 Wi-Fi P2P) allows mobile devices to discover, pair, and establish high-speed wireless links without requiring an external Wi-Fi router, cellular connection, or internet gateway.

```
       [ Group Owner (GO) ] <====================> [ Client (CLI) ]
         OPPO Reno                                  Xiaomi Redmi 13 5G
         IP: 192.168.49.1                           IP: 192.168.49.60
         Virtual MAC: 02:00:00:00:00:01             Virtual MAC: 02:00:00:00:00:02
         Subnet: 192.168.49.0/24                    Subnet: 192.168.49.0/24
```

### 1.1 Group Formation Lifecycle
1. **Peer Discovery**: `WifiP2pManager.discoverPeers()` sends Wi-Fi probe requests across 2.4 GHz and 5 GHz channels.
2. **Invitation & WPS Handshake**: `WifiP2pManager.connect()` with `WpsInfo.PBC` (Push Button Configuration) initiates the P2P group negotiation.
3. **Role Determination**: The device with higher `groupOwnerIntent` (set to 15 in [`WiFiDirectManager.kt`](file:///home/nkandasamy/Desktop/Joker_protocol/android/app/src/main/java/com/example/jokerprotocol/WiFiDirectManager.kt)) becomes the Group Owner (GO).
4. **IP Provisioning**: The Android kernel starts an internal DHCP server on the GO. The GO always receives `192.168.49.1`, while connecting clients receive addresses in the `192.168.49.x` subnet (e.g. `192.168.49.60`).

### 1.2 Critical Kernel Invariant: Hotspot vs. Wi-Fi Direct
When standard Android Mobile Hotspot (Tethering) is turned ON, the kernel network stack executes `nl80211: deinit ifname=p2p0`, destroying the Wi-Fi Direct interface.
- **Rule**: Mobile Hotspot must remain **OFF** during Wi-Fi Direct operation. If host laptop connectivity is needed, developers must use **USB Tethering**, which leaves the phone's Wi-Fi radio 100% available for P2P.

---

## 2. Multi-Device Real-World Testbed Validation

The protocol was validated over-the-air between two physical Android devices:
1. **Device 1**: Xiaomi Redmi 13 5G (Android 15, API 35, Serial: `e525fef3`)
2. **Device 2**: OPPO Reno (Android 14, API 34, Serial: `BELBUCW4U8RCBIIR`)

### Verified Test Logcat Evidence (Redmi 13):
```log
10-12 22:29:22.050 I JokerUdpNic: UdpNicAdapter started on port 5005 with Virtual MAC: 02:00:00:00:00:02
10-12 22:29:22.050 I JokerProtocol: JOKER Protocol [Node CLI] created with MAC 02:00:00:00:00:02 on port 5005
10-12 22:29:22.051 I JokerProtocol: Started Node CLI bound to IP 192.168.49.60 on port 5005
10-12 22:29:22.051 I JokerUdpNic: Peer IP explicitly set to: 192.168.49.1
10-12 22:29:22.051 I JokerProtocol: Successfully injected Client Unicast Peer IP from Kotlin: 192.168.49.1
10-12 22:29:32.601 I JokerProtocol: [Node CLI] Sent payload (id=3057649982): hi
10-12 22:29:41.226 I JokerProtocol: [Node CLI] RECEIVED PACKET: hello
10-12 22:30:11.146 I JokerUdpNic: Stopping UdpNicAdapter...
10-12 22:30:11.147 I JokerUdpNic: UdpNicAdapter stopped.
```

**Key Takeaways from Hardware Run**:
- Direct delivery succeeded immediately.
- Zero duplicate frames delivered to UI.
- Zero self-echoes observed when transmitting.
- Clean start and stop lifecycles without thread hangs.

---

## 3. P2P File & Media Streaming Protocol

While raw text chat messages fit easily inside a single JOKER frame, real-world media files (JPEG/PNG photos, PDF documents, audio clips) exceed the ~1400-byte UDP MTU limit.

To enable rich media transfer over JOKER, an application-layer **Chunking, Pacing, and Reassembly Protocol** was implemented.

### 3.1 Framing Specification

Three distinct control prefixes govern the media transfer session:

```
[ Session Initiation: FT_META ]
FT_META:<transfer_id>:<file_name>:<file_size_bytes>:<total_chunks>:<mime_type>
Example: FT_META:7a9f2b1c:photo.jpg:124800:156:image/jpeg

[ Chunk Transmission: FT_CHUNK ]
FT_CHUNK:<transfer_id>:<chunk_index>:<base64_encoded_payload>
Example: FT_CHUNK:7a9f2b1c:0:/9j/4AAQSkZJRgABAQEASABIAAD/2wBD...

[ Session Completion: FT_DONE ]
FT_DONE:<transfer_id>
Example: FT_DONE:7a9f2b1c
```

### 3.2 Slicing & Pacing Engine

1. **Chunk Size**: Slices raw binary data into chunks of **800 bytes**.
   - 800 bytes of binary yields 1,068 Base64 characters.
   - Total JOKER wire frame size: ~1,100 bytes (safely below 1,400 bytes, avoiding IP packet fragmentation).
2. **Inter-Frame Pacing**:
   - Transmitting 200 UDP frames in a tight `while` loop overburdens the Linux kernel socket buffers, causing dropped frames.
   - The transmitter coroutine uses an **8 ms inter-frame pacing delay** (`kotlinx.coroutines.delay(8)`).
   - A 50 KB image (63 chunks) completes transmission in ≈**550 ms**.
   - A 200 KB image (250 chunks) completes in ≈**2.0 seconds**.

### 3.3 Reassembly & Storage Architecture

1. Upon receiving `FT_META`, the receiver allocates an `IncomingTransfer` object with an internal `ConcurrentHashMap<Int, ByteArray>()`.
2. As `FT_CHUNK` packets arrive, chunks are decoded and placed into the map by index.
3. Upon receiving `FT_DONE`, the receiver writes all chunks sequentially into:
   `context.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS)/<fileName>`
4. **Zero Permission Requirement**: Because `getExternalFilesDir()` is app-specific scoped storage, it requires **no runtime storage permissions** across Android 13, 14, and 15.

---

## 4. Jetpack Compose UI Features

The Android app provides a modern, responsive user experience:
- **Tabbed Interface**: Switch smoothly between **"💬 Chat & Media"** and **"📋 Logs"**.
- **Real-Time Transfer Progress**: Displays animated linear progress bars with chunk count and percentage:
  `📤 Sending: photo.jpg (65%)` / `📥 Receiving: photo.jpg (65%)`.
- **Inline Image Thumbnails**: Decodes received image files in memory using `BitmapFactory` and renders them directly inside chat bubbles.
- **Fullscreen Image Viewer**: Tapping any image thumbnail opens a high-resolution dialog view.
- **Document Cards & External Viewer**: PDF/text files display with a document card and an "Open File" button that launches an Android `ACTION_VIEW` intent via `FileProvider`.
- **One-Tap Demo Generators**:
  - 🎨 **"Send Demo Photo"**: Generates a 450x300 high-res gradient test image with timestamp and transmits it in 1 tap.
  - 📄 **"Send Demo Spec Doc"**: Generates an excerpt specification text file and transmits it.
  - 📎 **"Choose Photo / File"**: Opens the system file picker (`ActivityResultContracts.GetContent()`) to pick any real file from the device.
