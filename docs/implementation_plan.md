# True P2P JOKER Protocol Implementation Plan

You make an excellent point regarding projects like Deadnet. You are completely correct that Android supports **true, offline Peer-to-Peer (P2P) connections without needing an internet connection or a conventional home router**. 

While Android blocks raw MAC-layer frame injection (which the paper strictly used), we can achieve **true offline P2P** by leveraging Android's **Wi-Fi Direct (Wi-Fi P2P) API**. 

## The P2P Solution
1. **Wi-Fi Direct (P2P):** We will use Android's native Wi-Fi Direct APIs to allow the phones to discover each other and form an isolated, offline mesh group. One phone dynamically becomes the "Group Owner" (like an invisible, offline router), and the others connect to it. This requires **no internet** and **no home Wi-Fi**.
2. **UDP Virtual Link Layer:** Once the Wi-Fi Direct group is formed, the phones are assigned local IP addresses on this isolated P2P network. We will then bind our C++ JOKER UDP Adapter to this specific Wi-Fi Direct network interface to broadcast packets. 

This satisfies the JOKER protocol's requirement for a "broadcast medium" while honoring the true offline, P2P nature of your project!

## Proposed Changes

### 1. Android UI & Wi-Fi Direct Integration
- **[MODIFY]** `AndroidManifest.xml`: Add required permissions (`ACCESS_FINE_LOCATION`, `NEARBY_WIFI_DEVICES`, `CHANGE_WIFI_STATE`, `INTERNET` - needed for local sockets, not web access).
- **[NEW]** `WiFiDirectManager.kt`: A helper class to manage Wi-Fi Direct peer discovery and group connection.
- **[MODIFY]** `MainScreen.kt`: 
  - Add a "Discover Peers" button to find other offline phones.
  - Add a list of discovered peers to connect to.
  - A "Start JOKER Protocol" button that initializes the C++ core over the P2P connection.
  - A "Send Test Message" chat box.
  - A scrolling Log Viewer to see the JOKER protocol's internal routing metrics and OGM broadcasts.

### 2. C++ Core / JNI Updates
- **[NEW]** `adapters/android_jni/udp_nic_adapter.cpp`: Implements the `joker::NicAdapter` interface. It will open a UDP socket bound to the Wi-Fi Direct interface and use UDP Broadcast (e.g., `255.255.255.255` or subnet broadcast) to simulate the MAC-layer wireless medium.
- **[MODIFY]** `adapters/android_jni/joker_jni.cpp`: Expose JNI methods for the UI to inject messages into the protocol and read the logs.

## Verification Plan
### Manual Verification
1. You and your teammate will turn on Wi-Fi (but forget/disconnect from any home networks to prove it works offline).
2. Open the app on both phones and tap "Discover Peers".
3. Connect the phones via the app's Wi-Fi Direct UI.
4. Tap "Start JOKER Protocol". You will see the C++ core immediately start broadcasting OGM control messages over the offline P2P link.
5. Send a chat message and watch JOKER wrap it in its custom header and route it to the other device.

> [!TIP]
> This approach gives you the absolute best of both worlds: It proves the C++ protocol works, it requires zero external hardware or internet, and it runs natively on your unrooted Android phones.

## Open Questions
- Does this Wi-Fi Direct (true offline P2P) approach sound exactly like what you need for the demo? 
