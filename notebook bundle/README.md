# JOKER Protocol — NotebookLM Documentation Bundle

Welcome to the **JOKER Protocol Knowledge Bundle**. This collection of documentation files is specifically curated for ingestion into **NotebookLM** (or other LLM reasoning environments) to provide complete, comprehensive, and grounded context on the theoretical foundations, mathematical derivations, C++20 architecture, Linux/Android implementations, and multi-device real-world testbed results of the JOKER Protocol.

---

## Document Index & Reading Guide

0. [`00_Original_IEEE_Research_Paper.pdf`](00_Original_IEEE_Research_Paper.pdf)
   - **The primary source publication**: *"JOKER: A Novel Opportunistic Routing Protocol"* by Ramon Sanchez-Iborra and Maria-Dolores Cano (IEEE JSAC, May 2016). Contains the full theoretical model, simulations, and benchmark results against BATMAN.

1. [`01_Executive_Summary_and_Protocol_Foundations.md`](01_Executive_Summary_and_Protocol_Foundations.md)
   - The fundamental shift: Single-Next-Hop (BATMAN/AODV) vs. Opportunistic Routing.
   - The wireless broadcast nature and Virtual Link ETX reduction ($P_{\text{virtual}} = 1 - \prod (1 - p_i)$).
   - Core trade-offs: Reliability, Delay, QoE, and Energy.

2. [`02_Mathematical_Formulations_and_Routing_Metrics.md`](02_Mathematical_Formulations_and_Routing_Metrics.md)
   - Transmission Quality (TQ) derivation: Forward Quality ($LQ$), Reverse Quality ($RQ$), and Bidirectional $TQ$.
   - The `Distance_penalty` metric and RSS fade margin formula.
   - Dynamic Control Message Sending Interval (CMSI) adaptive equation.
   - ITU-T Mean Opinion Score (MOS) video quality model.

3. [`03_Packet_Formats_and_State_Machines.md`](03_Packet_Formats_and_State_Machines.md)
   - Binary wire-format layout (13-byte fixed header + candidate array + CRC-32 packet ID).
   - Central RX pipeline decision tree (`process_received_frame`).
   - Deduplication Cache ($O(1)$ LRU with 5000ms sliding TTL).

4. [`04_Coordination_Schemes_Timer_vs_ACK.md`](04_Coordination_Schemes_Timer_vs_ACK.md)
   - Deep dive into **JOKER-Timer** (delay-slotted timer wheel, overhearing cancellation, zero control overhead).
   - Deep dive into **JOKER-ACK** (two-phase ACK/Forwarding handshake, zero duplicate forwardings).
   - Comprehensive trade-off comparison matrix.

5. [`05_Implementation_Architecture_and_Engineering_Resolutions.md`](05_Implementation_Architecture_and_Engineering_Resolutions.md)
   - C++20 Core Library Architecture (`libjoker`).
   - Concurrency race condition fixes (mutex locks on `pending_` forward vectors).
   - Background timer wheel ticker thread.
   - CRC-32 + atomic sequence counter for collision-free packet IDs.
   - Virtual NIC adapters (Linux `AF_PACKET` raw sockets & Android UDP sockets).

6. [`06_Android_Wi-Fi_Direct_and_P2P_Media_Transfer.md`](06_Android_Wi-Fi_Direct_and_P2P_Media_Transfer.md)
   - Android Wi-Fi Direct (P2P) Group Owner (GO) vs. Client (CLI) lifecycle.
   - Real-world 2-device testbed validation (Xiaomi Redmi 13 5G & OPPO Reno).
   - P2P File & Media Streaming Protocol: 800-byte chunking, Base64 framing, 8ms pacing.
   - Jetpack Compose interactive chat & media UI with inline image thumbnails.

7. [`07_Source_Code_Reference_and_File_Index.md`](07_Source_Code_Reference_and_File_Index.md)
   - Complete file-by-file directory index across `include/`, `src/`, `adapters/`, `android/`, and `cli/`.
   - Function-level reference for all major components.

8. [`08_Full_Architecture_Specification.md`](08_Full_Architecture_Specification.md)
   - Original comprehensive system specification mapping IEEE JSAC paper requirements.

9. [`09_Full_Implementation_Roadmap.md`](09_Full_Implementation_Roadmap.md)
   - Step-by-step implementation guide and compliance checkpoints.

10. [`10_Presentation_Slides.pdf`](10_Presentation_Slides.pdf)
    - Visual conference slide deck summarizing protocol motivation, equations, and evaluation.

11. [`11_WiFi_Direct_Diagnostic_Guide.md`](11_WiFi_Direct_Diagnostic_Guide.md)
    - In-depth field diagnostic guide for Android Wi-Fi Direct state management and peer discovery.

12. [`12_Multicast_and_Broadcast_Diagnostics.md`](12_Multicast_and_Broadcast_Diagnostics.md)
    - Detailed analysis of Android multicast packet drops and the subnet broadcast pivot over Wi-Fi Direct.

---

## Suggested Questions to Ask NotebookLM

Once uploaded into NotebookLM, you can query:
- *"How does JOKER reduce the expected number of transmissions compared to single-next-hop routing protocols like BATMAN?"*
- *"Explain the difference between JOKER-Timer and JOKER-ACK coordination schemes and when to choose each."*
- *"What mathematical equation governs the Distance Penalty, and why does it prevent excessively long routing paths?"*
- *"How does the Dynamic Control Message Sending Interval (CMSI) save battery power during high-volume video streaming?"*
- *"How does the Android P2P file streaming protocol handle MTU limits and prevent socket buffer drops over Wi-Fi Direct?"*
- *"What race conditions and timer issues were discovered during the audit, and how were they resolved in C++20?"*
