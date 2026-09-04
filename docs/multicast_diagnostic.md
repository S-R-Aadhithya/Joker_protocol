# JOKER Protocol: UDP Multicast Diagnostic Checklist

This document outlines a comprehensive diagnostic checklist for debugging cross-device UDP Multicast failures between a Linux laptop and an Android phone, specifically when self-loopback works but cross-device packets are dropped.

> [!NOTE]
> The evidence that both devices receive their *own* loopback packets proves that the application logic, serialization, and basic socket creation are completely correct. The issue lies in network routing, interface selection, or infrastructure drops.

## 1. Enterprise Wi-Fi AP Isolation (The Campus Wi-Fi Problem)

Enterprise networks (like college campus Wi-Fi) almost universally enable features that kill peer-to-peer multicast.

*   **Client/AP Isolation:** Prevents devices connected to the same access point from seeing or communicating with each other to prevent lateral attacks.
*   **Multicast Suppression:** Multicast traffic is often dropped entirely by enterprise controllers to preserve airtime.

> [!CAUTION]
> **Action Required:** You cannot use multicast reliably on a college/enterprise network. You must test on a standard home router where you have admin access to ensure AP Isolation is turned **OFF** and IGMP Snooping/Multicast is allowed.

## 2. Linux Routing Issues & `INADDR_ANY` (The Docker/VM Bridge Problem)

When you use `INADDR_ANY` and do not explicitly bind the sending socket to a specific interface, the Linux kernel has to guess where to route the multicast packet.

*   **The Issue:** By default, if you have Docker, VirtualBox, or VPNs installed, Linux often defaults to routing multicast out of `docker0`, `virbr0`, or `tun0` instead of your physical Wi-Fi card.
*   **The Symptom:** The packet goes out the virtual bridge. Your socket sees the loopback (because it was sent from the local machine), but the packet never hits the actual Wi-Fi radio.

### The Fix (Sender)
You **must** explicitly set `IP_MULTICAST_IF` on the sending socket to the IP address of your Wi-Fi interface.
```cpp
struct in_addr localInterface;
localInterface.s_addr = inet_addr("192.168.1.100"); // The Linux laptop's Wi-Fi IP
setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (char *)&localInterface, sizeof(localInterface));
```

### The Fix (Receiver)
When joining the group, do not use `INADDR_ANY`. Specify the Wi-Fi interface's IP.
```cpp
struct ip_mreq group;
group.imr_multiaddr.s_addr = inet_addr("239.1.2.3");
group.imr_interface.s_addr = inet_addr("192.168.1.100"); // Listen explicitly on Wi-Fi
setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&group, sizeof(group));
```

> [!TIP]
> **Diagnostic Command:** Run `ip route get 239.1.2.3` on Linux. It will tell you exactly which interface the OS is choosing for the multicast packet.

## 3. Android Hotspot Routing Quirks

Testing over an Android Hotspot is notoriously difficult for multicast.

*   **The Issue:** When Android acts as a hotspot, it creates a bridge between the cellular data interface (`rmnet0`) and the hotspot Wi-Fi interface (e.g., `softap0`).
*   **The Quirks:** 
    1.  The Android kernel's tethering NAT rules (`iptables`) often drop multicast traffic originating from connected clients.
    2.  When the Android app itself sends a packet to `239.1.2.3`, the OS might route it out the cellular interface instead of the hotspot Wi-Fi interface.

> [!IMPORTANT]
> **Action Required:** Just like on Linux, your Android C++ code must query the device's IP address on the hotspot network (usually `192.168.43.1`) and explicitly set `IP_MULTICAST_IF` and `imr_interface` to that exact IP, rather than using `INADDR_ANY`.

## 4. Linux Firewall (UFW / iptables)

The loopback interface (`lo`) is almost always trusted by local firewalls. When your Linux machine sends the packet, the loopback copy is accepted by the firewall, but the incoming packet from the Android phone over Wi-Fi might be blocked.

*   **Diagnostic:** Check if `ufw` or `iptables` is blocking incoming UDP 5005. Run `sudo ufw status`. If active, run `sudo ufw allow 5005/udp`.
*   **Packet Sniffing:** Run Wireshark or `tcpdump` on the Linux laptop:
    ```bash
    sudo tcpdump -i <wifi-interface> udp port 5005
    ```
    If `tcpdump` sees the packets from the Android phone, but your C++ app doesn't, it is a local firewall issue (since `tcpdump` hooks in before the firewall).

## 5. Missing Socket Options & Permissions

Verify you have the following configurations in place:

### C++ Socket Options
*   **`IP_MULTICAST_TTL`**: The default Time-To-Live for multicast is `1`. This is fine for a single subnet, but some hotspot implementations do weird internal routing that decrements the TTL. Explicitly set it to `2` or `4`.
    ```cpp
    unsigned char ttl = 4;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    ```

### Android Java/Kotlin Side
*   **Permissions:** You have `CHANGE_WIFI_MULTICAST_STATE`, but ensure you also have `INTERNET`, `ACCESS_WIFI_STATE`, and `ACCESS_NETWORK_STATE`.
*   **MulticastLock Scope:** Ensure your `WifiManager.MulticastLock` object is held as a strong reference (e.g., a class-level variable) and `.acquire()` is called. If you declare it locally inside a function, the JVM garbage collector will destroy it, silently releasing the lock shortly after your socket starts.

---

## Summary Action Plan

1. **Change the Environment:** Stop testing on College Wi-Fi and Hotspots. Get a standard home router to test on.
2. **Explicit Interface Binding:** Stop using `INADDR_ANY`. Programmatically grab the Wi-Fi IP address on both devices and explicitly pass it to `IP_MULTICAST_IF` (for sending) and `IP_ADD_MEMBERSHIP` (for receiving).
3. **Check `tcpdump`:** Run `tcpdump` on the Linux machine to see if the Android packets are physically reaching the Wi-Fi card but being blocked by the Linux firewall.
4. **Check Linux Routing:** Run `ip route get 239.1.2.3` on Linux to ensure it isn't sending packets into a Docker/VM bridge.
