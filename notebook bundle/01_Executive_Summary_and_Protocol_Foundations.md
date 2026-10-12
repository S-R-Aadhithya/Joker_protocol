# 01 — Executive Summary & Protocol Foundations

## 1. Introduction & Overview

**JOKER** (*auto-adJustable Opportunistic acKnowledgment/timEr-based Routing*) is a proactive opportunistic routing protocol for wireless mesh and mobile ad-hoc networks (MANETs). Originally conceived by Ramon Sanchez-Iborra and Maria-Dolores Cano (*IEEE Journal on Selected Areas in Communications*, vol. 34, no. 5, May 2016), JOKER was specifically designed to handle high-bandwidth, delay-sensitive multimedia streams (such as live video streaming) while drastically reducing wireless network interface energy consumption.

Unlike legacy routing protocols that commit each transmission to a single next hop, JOKER leverages the **inherent broadcast nature of the wireless medium** to opportunistically forward packets across multiple candidate neighbors.

---

## 2. The Fundamental Paradigm Shift

### 2.1 Single-Next-Hop Routing vs. Opportunistic Routing

Traditional ad-hoc and mesh routing protocols—including **BATMAN** (Better Approach To Mobile Adhoc Networking), **OLSR** (Optimized Link State Routing), and **AODV** (Ad-hoc On-demand Distance Vector)—are **single-next-hop protocols**:

```
[ Traditional Single-Next-Hop Routing ]
Node S ──(commit to Hop A only)──> [ Hop A ] ──> Destination D
                                    (If link S->A fails, packet is lost!)
                                  [ Hop B ] (Overhears packet S->A, but discards it)
                                  [ Hop C ] (Overhears packet S->A, but discards it)
```

In single-next-hop routing:
1. The routing engine selects a single best neighbor for a given destination.
2. If that specific link experiences a momentary fade, collision, or interference burst, the packet is lost and must be retransmitted end-to-end.
3. If the link fails entirely due to mobility, an expensive route discovery or recomputation cycle is triggered.
4. **The fundamental waste**: Radio transmissions radiate omnidirectionally. Neighboring nodes (Hop B, Hop C) often overhear the packet cleanly, but legacy protocol stacks drop it because they are not designated as the single next hop.

### 2.2 The Opportunistic "Virtual Link" Concept

JOKER treats multiple candidate neighbors as a single **"Virtual Link"**:

```
[ JOKER Opportunistic Routing ]
                ┌──> [ Candidate 1 (Priority 1) ] ──┐
Node S ──(air)──┼──> [ Candidate 2 (Priority 2) ] ──┼──> Coordination ──> Destination D
                └──> [ Candidate 3 (Priority 3) ] ──┘     (Elects 1 forwarder)
```

If Candidate 1 misses the packet due to a momentary fade, Candidate 2 or Candidate 3 (who successfully overheard it) can step in and forward it immediately without requiring an end-to-end retransmission.

#### Mathematical Foundation: Expected Transmissions (ETX) Reduction

Consider a sender $S$ with three potential forwarders towards destination $D$, each with an independent packet delivery probability $p_i$:
- $p_1 = 0.5$ (Candidate 1)
- $p_2 = 0.4$ (Candidate 2)
- $p_3 = 0.3$ (Candidate 3)

Under single-next-hop routing, $S$ selects the best node (Candidate 1, $p_1 = 0.5$). The expected number of transmissions to successfully traverse this hop is:
$$\text{Expected Transmissions (Single)} = \frac{1}{p_1} = \frac{1}{0.5} = 2.0$$

Under JOKER's multi-candidate virtual link, the probability that **at least one** candidate receives the packet is:
$$P_{\text{virtual}} = 1 - \prod_{i=1}^{N} (1 - p_i)$$
$$P_{\text{virtual}} = 1 - (1 - 0.5)(1 - 0.4)(1 - 0.3) = 1 - (0.5 \times 0.6 \times 0.7) = 1 - 0.21 = 0.79$$

The expected number of transmissions drops from **2.0 to ≈1.26**:
$$\text{Expected Transmissions (Virtual)} = \frac{1}{P_{\text{virtual}}} = \frac{1}{0.79} \approx 1.265$$

In realistic heterogeneous wireless topologies, the paper demonstrates that this virtual link reduction decreases expected transmissions per packet from **3.5 down to ≈2.37**.

---

## 3. Core Trade-Offs Balanced by JOKER

| Vector | JOKER Mechanism | Primary Benefit |
| :--- | :--- | :--- |
| **Reliability** | Multi-candidate selection + BATMAN-derived Transmission Quality (TQ) metric | Drastically fewer dropped frames; link diversity absorbs wireless fading. |
| **Path Length** | Signal fade-margin `Distance_penalty` & "Lucky Long Transmission" direct delivery | Shorter hop count; packets jump directly to destination when propagation conditions allow. |
| **QoE / Delay** | Timer-based coordination (`JOKER-Timer`) with delay-slotted timer wheel | Eliminates control packet round-trips; delivers low latency critical for live video/audio. |
| **Energy Consumption** | Dynamic Control Message Sending Interval (CMSI) & reduced hop count | Cuts control plane radio broadcasts during active traffic; lets wireless interfaces enter low-power sleep. |
| **Mobility Resilience** | Pre-computed ordered backup candidate list in every packet header | If a moving node leaves range, secondary candidate immediately forwards with zero route renegotiation. |

---

## 4. Architectural Placement in the Network Stack

JOKER operates cleanly at **Layer 2.5** (between the Data Link Layer and Network Layer):

```
+-------------------------------------------------------+
|            Application Layer (Chat, Media, Video)      |
+-------------------------------------------------------+
|            Transport Layer (UDP / TCP / RTP)          |
+-------------------------------------------------------+
|            Network Layer (IPv4 / IPv6)                |
+-------------------------------------------------------+
|  >>> JOKER Protocol Engine (Layer 2.5) <<<            |
|   - Packet Classifier & Header Parser                 |
|   - DedupCache & Direct Delivery Check                |
|   - Candidate Selection & Timer Wheel Coordinator     |
|   - Transmission Quality (TQ) & CMSI State            |
+-------------------------------------------------------+
|     Network Interface Adapter (Linux AF_PACKET / UDP)  |
+-------------------------------------------------------+
|     Physical Medium (IEEE 802.11 Wi-Fi / Wi-Fi Direct)|
+-------------------------------------------------------+
```

Because JOKER sits below IP and above the physical adapter:
- Upper-layer applications require **zero modifications**.
- Standard IP packets are encapsulated inside JOKER frames.
- On Linux, it runs directly over raw Ethernet sockets (`ETH_P_JOKER` / `0x88B5`) in promiscuous mode.
- On Android, it runs over a high-performance virtual UDP NIC adapter leveraging Wi-Fi Direct P2P subnet broadcast.
