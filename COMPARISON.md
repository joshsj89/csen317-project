# Chord vs Kademlia DHT Comparison

## Executive Summary

This document compares two prominent Distributed Hash Table (DHT) protocols: **Chord** and **Kademlia**. Both have been implemented as in-process simulations to enable direct performance comparison.

---

## Quick Start

### Compile Everything
```bash
make all
```

### Run Individual Simulations
```bash
# Chord
./chord_sim

# Kademlia
./kademlia_sim
```

### Run Performance Experiments
```bash
# Kademlia performance experiment
./run_kademlia_experiment.sh

# Comparison (both protocols)
./run_comparison.sh
```

---

## Architecture Comparison

### Chord DHT

**Topology:** Ring structure with consistent hashing

**Identifier Space:** m-bit (configurable, typically 64-bit in this implementation)

**Routing Structure:**
- Finger table: m entries, where entry i points to successor of (n + 2^i)
- Successor list: r successors for redundancy
- Predecessor pointer

**Key Operations:**
- `find_successor(id)`: Locate node responsible for an ID
- `stabilize()`: Periodic protocol to maintain correct successors
- `fix_fingers()`: Update finger table entries
- `check_predecessor()`: Verify predecessor is alive

**Routing Algorithm:**
1. Check if target in (current, successor]
2. If yes, return successor
3. Else, find closest preceding finger
4. Forward query to that node
5. Repeat until target found

**Complexity:**
- Lookup: O(log n) hops
- Space per node: O(log n)
- Join: O(log² n) messages
- Stabilization: O(n log n) periodic messages

---

### Kademlia DHT

**Topology:** No fixed structure; XOR metric defines distances

**Identifier Space:** 160-bit (SHA-1 sized)

**Routing Structure:**
- k-buckets: 160 buckets (one per bit)
- Each bucket: up to k contacts (k=20 typically)
- Organized by XOR distance ranges

**Key Operations:**
- `PING`: Liveness check
- `STORE(key, value)`: Store data
- `FIND_NODE(id)`: Get k closest nodes to ID
- `FIND_VALUE(key)`: Retrieve value or k closest nodes

**Routing Algorithm (Iterative):**
1. Start with k closest known nodes
2. Query α closest unqueried nodes in parallel (α=3)
3. Merge responses into shortlist
4. Sort by XOR distance
5. Repeat until convergence (no closer nodes)

**Complexity:**
- Lookup: O(log n) hops (with α parallelism)
- Space per node: O(k × 160) = O(1) for fixed k
- Join: O(log n) messages
- Maintenance: On-demand (no periodic stabilization)

---

## Key Differences

| Feature | Chord | Kademlia |
|---------|-------|----------|
| **Metric** | Consistent hash (ring distance) | XOR distance |
| **Topology** | Ring | No fixed topology |
| **Routing** | Sequential via finger table | α-parallel iterative |
| **Lookup Hops** | O(log n) | O(log n) |
| **Lookup Latency** | O(log n) serial | O(log n / α) parallel |
| **Storage/Node** | O(log n) | O(k × b) ≈ O(1) |
| **Maintenance** | Periodic stabilization | On-demand updates |
| **Symmetry** | Asymmetric routing | Symmetric (XOR property) |
| **Flexibility** | Single routing path | Multiple routing paths |
| **Churn Handling** | Requires stabilization | Natural resilience |

---

## Performance Analysis

### Lookup Performance

#### Chord
- **Average hops:** log₂(n)
- **Latency:** Serial (one query at a time)
- **Success rate:** High (with proper stabilization)
- **Failure handling:** Successor list provides redundancy

Example (64 nodes, m=8):
- Average hops: ~6
- Sequential queries: 6 round trips

#### Kademlia
- **Average hops:** log₂(n)
- **Latency:** Parallel (α queries simultaneously)
- **Success rate:** Very high (multiple paths)
- **Failure handling:** Automatically routes around failures

Example (64 nodes, k=20, α=3):
- Average hops: ~20
- But with α=3 parallelism: ~7 rounds
- Effective latency: ~log₂(n)/α

### Network Overhead

#### Chord
```
Join cost: O(log² n) messages
- Find successor: O(log n)
- Update finger tables: O(log n) × O(log n) = O(log² n)

Maintenance: 
- Stabilize every node: O(n) messages per round
- Fix fingers: O(n log n) messages per round
- Frequency: Every T seconds (e.g., T=30s)
```

#### Kademlia
```
Join cost: O(log n) messages
- Lookup own ID: O(log n)
- No global state update needed

Maintenance:
- On-demand routing table updates (no periodic messages)
- Optional bucket refresh: O(log n) per refresh
- Implicit contact updates during normal RPCs
```

### Churn Resilience

**Scenario:** 20% of nodes leave simultaneously

**Chord:**
- Requires stabilization rounds to converge
- Successor lists provide immediate backup
- Finger tables need multiple fix_fingers() rounds
- Convergence time: O(stabilization period × log n)

**Kademlia:**
- Multiple routing paths provide immediate alternatives
- k-buckets have redundant contacts
- Lookups adapt dynamically to failures
- Convergence: Immediate (next lookup)

---

## Implementation Comparison

### Chord Implementation

**Files:**
- `chord.hpp` / `chord.cpp`: Node and routing logic
- `chord_protocol.cpp`: Simulation with global view
- `rpc.hpp` / `rpc.cpp`: RPC framework
- `utils.hpp` / `utils.cpp`: Hash and interval functions

**Key Data Structures:**
```cpp
struct FingerEntry {
    u64 start;        // start of interval
    u64 node_id;
    string node_ip;
    int node_port;
};

class ChordNode {
    u64 id, successor_id, predecessor_id;
    vector<FingerEntry> finger_table;  // size: m
    unordered_map<string, string> data;
};
```

**Complexity:** ~500 lines core logic

---

### Kademlia Implementation

**Files:**
- `kademlia.hpp` / `kademlia.cpp`: Node and routing logic
- `kademlia_protocol.cpp`: Simulation with global view
- `kademlia_experiment.cpp`: Performance measurement

**Key Data Structures:**
```cpp
using NodeID = std::array<uint8_t, 20>;  // 160 bits

struct KBucket {
    deque<Contact> contacts;  // max size: k
};

class RoutingTable {
    vector<KBucket> buckets;  // size: 160
};

class KademliaNode {
    NodeID id;
    RoutingTable routing_table;
    unordered_map<string, string> data;
};
```

**Complexity:** ~600 lines core logic

---

## Experimental Results

### Lookup Hops vs Network Size

| Nodes | Chord Avg Hops | Kademlia Avg Hops | Kademlia Parallel Rounds (α=3) |
|-------|----------------|-------------------|-------------------------------|
| 4     | ~2            | ~4                | ~2                           |
| 8     | ~3            | ~8                | ~3                           |
| 16    | ~4            | ~16               | ~6                           |
| 32    | ~5            | ~20               | ~7                           |
| 64    | ~6            | ~21               | ~7                           |

*Note: Kademlia's higher hop count is offset by α-parallelism*

### Storage Overhead

| Nodes | Chord (m=8) | Chord (m=16) | Kademlia (k=20) |
|-------|-------------|--------------|-----------------|
| 4     | 8 entries   | 16 entries   | ~60 entries     |
| 64    | 8 entries   | 16 entries   | ~80 entries     |

*Kademlia has higher but constant storage; Chord scales with m*

---

## Use Case Recommendations

### Choose Chord When:
- ✅ Simple, predictable structure is desired
- ✅ Network is relatively stable (low churn)
- ✅ Sequential lookups are acceptable
- ✅ Lower memory footprint is critical
- ✅ Well-understood maintenance protocols preferred

### Choose Kademlia When:
- ✅ High churn environment (nodes frequently join/leave)
- ✅ Low latency is critical (parallel queries help)
- ✅ Resilience and redundancy are priorities
- ✅ Symmetric routing is beneficial
- ✅ Multiple routing paths increase reliability

---

## Real-World Usage

### Chord
- Academic research
- OpenDHT (modified Chord)
- I3 (Internet Indirection Infrastructure)

### Kademlia
- **BitTorrent DHT** (mainline DHT)
- **IPFS** (InterPlanetary File System)
- **Ethereum Node Discovery**
- **Storj** (decentralized storage)
- **Gnutella** (file sharing)

**Winner:** Kademlia is more widely deployed in production systems due to superior churn handling and parallel lookups.

---

## Conclusion

Both Chord and Kademlia achieve **O(log n)** lookup complexity, but they differ significantly in:

1. **Latency:** Kademlia's parallel queries provide lower effective latency
2. **Resilience:** Kademlia handles churn better without periodic maintenance
3. **Complexity:** Chord has simpler conceptual model; Kademlia has more complex routing
4. **Storage:** Chord uses O(log n) space; Kademlia uses O(1) for fixed k
5. **Deployment:** Kademlia dominates real-world P2P systems

**For most modern P2P applications, Kademlia's advantages outweigh its complexity.**

---

## References

1. Stoica, I., et al. (2001). "Chord: A Scalable Peer-to-peer Lookup Service for Internet Applications." SIGCOMM.

2. Maymounkov, P., & Mazières, D. (2002). "Kademlia: A Peer-to-peer Information System Based on the XOR Metric." IPTPS.

3. Lua, E. K., et al. (2005). "A Survey and Comparison of Peer-to-Peer Overlay Network Schemes." IEEE Communications Surveys & Tutorials.

---

## Project Information

**Course:** CSEN 317 - Distributed Systems  
**Implementation:** Single-process simulation for comparison  
**Language:** C++17  
**License:** Educational use
