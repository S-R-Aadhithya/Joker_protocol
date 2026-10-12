# 02 — Mathematical Formulations & Routing Metrics

This document details the complete mathematical framework underlying JOKER's candidate ranking, link quality evaluation, adaptive control signaling, and multimedia Quality of Experience (QoE) models.

---

## 1. Transmission Quality (TQ) Metric

JOKER inherits and refines the Transmission Quality (TQ) philosophy from the B.A.T.M.A.N. protocol. Unlike simple hop-count metrics (which often select long, flaky links), TQ continuously tracks bidirectional packet delivery success across a sliding window of $W$ periodic broadcast packets (Originator Messages or OGMs, default $W = 128$).

### 1.1 Forward Link Quality ($LQ$)
Forward Link Quality measures the fraction of OGMs that the local node $A$ successfully receives from neighbor $B$:

$$LQ(A \leftarrow B) = \frac{\text{OGMs received from } B \text{ within window } W}{W}$$

Where:
- $0.0 \le LQ \le 1.0$
- Evaluated passively from sequence numbers carried in incoming OGMs.

### 1.2 Reverse Link Quality ($RQ$)
Wireless channels are frequently asymmetric (e.g., due to transmit power differences, local noise floors, or antenna orientation). Node $A$ needs to know the probability that neighbor $B$ successfully receives packets sent by $A$.

Node $B$ piggybacks its own measured $LQ(B \leftarrow A)$ in its outgoing OGMs. When node $A$ receives node $B$'s OGM, it extracts this value as Reverse Quality:

$$RQ(A \rightarrow B) = LQ(B \leftarrow A)$$

### 1.3 Bidirectional Transmission Quality ($TQ$)
The true bidirectional probability of successfully transmitting a packet and receiving its link-layer confirmation across the link $A \leftrightarrow B$ is the product of forward and reverse qualities:

$$TQ_{A \leftrightarrow B} = LQ(A \leftarrow B) \times RQ(A \rightarrow B)$$

- $TQ \in [0.0, 1.0]$
- $TQ = 1.0$ represents a flawless, lossless bidirectional link.
- $TQ \le 0.2$ indicates a severe, lossy link pruned from candidate consideration.

---

## 2. Distance Penalty Metric ($\text{Distance\_penalty}$)

Standard TQ metrics tend to favor very short physical hops because short hops exhibit high SNR and low packet loss. However, stringing together dozens of short hops increases the overall hop count, which multiplies total network channel contention, forwarding latency, and radio power consumption.

JOKER introduces a **Distance Penalty** to strike an optimal balance between link reliability and spatial progress towards the destination.

### 2.1 Received Signal Strength (RSS) & Fade Margin
The local physical layer measures the Received Signal Strength Indicator (RSSI) of incoming packets:
- $RSS$ (in dBm): Measured signal level of the neighbor's transmission.
- $RSS_{\text{thresh}}$ (in dBm): Minimum receiver sensitivity threshold for the current modulation and coding scheme (MCS).
- $RSS_{\text{target}}$ (in dBm): Safe target operating point (typically $RSS_{\text{thresh}} + \Delta_{\text{margin}}$, e.g. 15–20 dB fade margin).

### 2.2 Penalty Formulation
The penalty is calculated as:

$$\text{Distance\_penalty} = \max\left(0, \frac{RSS - RSS_{\text{target}}}{RSS_{\text{max}} - RSS_{\text{target}}}\right) \times \alpha$$

Where:
- $\alpha \in [0, 1]$ is a tunable weighting parameter (typically $\alpha = 0.2$).
- If a node is very close, its $RSS \gg RSS_{\text{target}}$, yielding a higher penalty that slightly depresses its composite score.
- If a node is farther away while maintaining adequate fade margin ($RSS \approx RSS_{\text{target}}$), the penalty approaches zero.

### 2.3 Composite Candidate Metric ($M$)
The final metric used to sort and rank candidate forwarders in the neighbor table is:

$$M = TQ - \text{Distance\_penalty}$$

Neighbors with the highest composite metric $M$ are elected into the candidate set:
$$\text{Candidates} = \arg\max_{c \in \text{Neighbors}, |c| = N_{\text{candidates}}} \{ M_c \}$$

---

## 3. Dynamic Control Message Sending Interval (CMSI)

In proactive routing protocols, periodic control beacons (OGMs) consume finite wireless spectrum and prevent mobile device wireless chipsets from entering low-power sleep states.

JOKER adapts the beaconing frequency dynamically based on current user data traffic. When traffic volume is high, routes are actively tested and exercised by user data frames, allowing the control beacon rate to be relaxed:

$$CMSI(t) = \min\left(CMSI_{\text{max}}, \; CMSI_{\text{base}} \times \left(1 + \beta \cdot \frac{\text{Throughput}_{\text{data}}(t)}{\text{Throughput}_{\text{capacity}}}\right)\right)$$

Where:
- $CMSI_{\text{base}}$: Baseline beaconing interval when network is idle (default = 1000 ms).
- $CMSI_{\text{max}}$: Upper bound on the beacon interval (default = 5000 ms).
- $\beta$: Acceleration coefficient.
- $\text{Throughput}_{\text{data}}(t)$: Measured bytes transmitted/received over the last epoch.

**Result**: In periods of heavy video streaming or file transfer, routing protocol airtime overhead drops by up to **60%**, conserving significant battery power.

---

## 4. Delay Coordination Scheduling

In the **JOKER-Timer** coordination scheme, candidate nodes that receive a data packet schedule an opportunistic forwarding timer. To ensure candidates do not transmit simultaneously and collide over the air, their timer delay is proportional to their assigned candidate priority:

$$T_{\text{delay}}(i) = T_{\text{wait}} \times (i - 1)$$

Where:
- $i \in \{1, 2, \dots, N_{\text{candidates}}\}$ is the node's 1-based candidate priority ranking in the packet header.
- $T_{\text{wait}}$ is the inter-candidate slot spacing duration (default = 10 ms to 20 ms, covering packet airtime + DIFS + processing jitter).

**Evaluation**:
- **Candidate 1 (Primary)**: $T_{\text{delay}}(1) = T_{\text{wait}} \times (1 - 1) = 0 \text{ ms}$. Forwards immediately.
- **Candidate 2 (Secondary)**: $T_{\text{delay}}(2) = T_{\text{wait}} \times (2 - 1) = T_{\text{wait}}$ (e.g. 10 ms).
- **Candidate 3 (Tertiary)**: $T_{\text{delay}}(3) = T_{\text{wait}} \times (3 - 1) = 2 \cdot T_{\text{wait}}$ (e.g. 20 ms).

If Candidate 1 forwards cleanly at $t = 0$, Candidate 2 and Candidate 3 overhear Candidate 1's transmission, query their `TimerCoordinator`, and cancel their pending timers before they expire!

---

## 5. Video Quality Estimation: Mean Opinion Score (MOS) Model

To evaluate multimedia streaming performance, JOKER adopts the ITU-T Mean Opinion Score (MOS) mapping model for H.264 video traffic:

$$MOS = 1 + 0.035 \cdot R + R \cdot (R - 60) \cdot (100 - R) \cdot 7 \cdot 10^{-6}$$

Where $R$ is an effective quality transmission rating factor derived from video encoding parameters and packet loss:

$$R = 100 - 95 \times \left(\frac{P_{\text{loss}}}{P_{\text{loss}} + D_{\text{loss}}}\right)$$

Where:
- $P_{\text{loss}}$: End-to-end packet loss ratio experienced by the video stream.
- $D_{\text{loss}}$: Loss sensitivity factor associated with video frame structure (I-frame vs. P-frame vulnerability).

### MOS Rating Scale:
| MOS Score | Subjective Quality | User Perception |
| :---: | :---: | :--- |
| **5.0** | Excellent | Imperceptible degradation |
| **4.0** | Good | Perceptible but not annoying |
| **3.0** | Fair | Noticeably annoying artifacts |
| **2.0** | Poor | Very annoying, broken frames |
| **1.0** | Bad | Completely unwatchable |

Under high network congestion and mobility in the IEEE JSAC paper, JOKER sustained MOS scores above **4.1**, whereas legacy single-hop BATMAN degraded below **2.8** due to consecutive dropped frame bursts.
