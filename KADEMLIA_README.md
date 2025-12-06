# Kademlia DHT Implementation

## Overview

This is a simulation-based implementation of the Kademlia Distributed Hash Table (DHT) protocol for comparison with Chord DHT. The implementation includes all core Kademlia features:

- 160-bit node identifiers
- XOR distance metric
- k-bucket routing tables
- Four core RPCs (PING, STORE, FIND_NODE, FIND_VALUE)
- α-parallel iterative lookups
- Node join procedure

## File Structure

- `kademlia.hpp` - Header file with data structures and class definitions
- `kademlia.cpp` - Implementation of core Kademlia logic
- `kademlia_protocol.cpp` - Main simulation program
- `run_kademlia.sh` - Script to compile and run Kademlia simulation
- `run_comparison.sh` - Script to run both Chord and Kademlia for comparison

## Building and Running

### Compile Kademlia Only
```bash
make kademlia_sim
```

### Run Kademlia Simulation
```bash
./kademlia_sim
# or
./run_kademlia.sh
```

### Run Comparison Between Chord and Kademlia
```bash
./run_comparison.sh
```

This will generate `chord_results.txt` and `kademlia_results.txt` for analysis.

## Implementation Details

### Node ID Generation
- Uses 160-bit identifiers (same as SHA-1 hash size)
- Node IDs generated from hash of node name
- Stored as `std::array<uint8_t, 20>`

### XOR Distance Metric
```cpp
NodeID xor_distance(const NodeID& a, const NodeID& b);
```
- Computes bitwise XOR between two node IDs
- Used for all distance calculations
- Provides symmetric routing property

### K-Bucket Routing Table
- Each node maintains 160 k-buckets (one per bit)
- Each bucket holds up to k contacts (default: k=20)
- Buckets organized by XOR distance
- Least-recently-seen eviction policy

### Core RPCs

#### 1. PING
```cpp
RPCResponse handle_ping(const RPCRequest& req);
```
- Checks node liveness
- Updates routing table with sender information

#### 2. STORE
```cpp
RPCResponse handle_store(const RPCRequest& req);
```
- Stores (key → value) pair on the node
- Updates routing table

#### 3. FIND_NODE(targetID)
```cpp
RPCResponse handle_find_node(const RPCRequest& req);
```
- Returns k closest nodes to target ID
- Uses routing table to find closest contacts

#### 4. FIND_VALUE(key)
```cpp
RPCResponse handle_find_value(const RPCRequest& req);
```
- Returns value if node has it
- Otherwise returns k closest nodes to key

### Lookup Algorithm

#### Iterative Node Lookup
```cpp
std::vector<Contact> iterative_find_node(const NodeID& target, int& hops);
```

**Algorithm:**
1. Start with k closest known nodes to target
2. Send FIND_NODE to α closest unqueried nodes (parallel)
3. Merge responses into shortlist
4. Sort by XOR distance to target
5. Repeat until convergence (no closer nodes found)

**Parameters:**
- k = 20 (bucket size)
- α = 3 (concurrency factor)

**Complexity:** O(log n) hops

#### Iterative Value Lookup
```cpp
std::optional<std::string> iterative_find_value(const std::string& key, int& hops);
```

Similar to node lookup but uses FIND_VALUE RPC and terminates early if value is found.

### Node Join Procedure
```cpp
void join(const Contact& bootstrap_node);
```

**Steps:**
1. Add bootstrap node to routing table
2. Perform lookup for own node ID
3. Populate routing table with returned contacts
4. Optionally refresh all k-buckets

### Replication

Keys are stored on the k closest nodes to the key's hash:
- Default replication factor: 3
- Provides redundancy and availability
- Handles node departures gracefully

## Chord vs Kademlia Comparison

### Chord DHT
**Structure:**
- Ring topology with consistent hashing
- m-bit identifier space (configurable)
- Each node maintains O(log n) finger table entries
- Successor and predecessor pointers

**Routing:**
- Lookups via finger table navigation
- O(log n) hops on average
- Sequential lookups

**Maintenance:**
- Periodic stabilization protocol
- Fix_fingers() for routing table maintenance
- Check_predecessor() for failure detection

**Advantages:**
- Simpler structure and implementation
- Well-defined successor relationships
- Proven stability with stabilization protocol

**Disadvantages:**
- Single lookup path (less flexible)
- Stabilization overhead
- Slower adaptation to network changes

### Kademlia DHT
**Structure:**
- No ring topology
- 160-bit identifier space
- k-bucket routing tables (k=20 typically)
- XOR metric for distance

**Routing:**
- α-parallel iterative lookups (α=3)
- O(log n) hops with parallelism
- Multiple routing paths to any destination

**Maintenance:**
- Implicit routing table updates during RPCs
- Optional bucket refresh
- LRU eviction for stale contacts

**Advantages:**
- Parallel queries reduce latency
- Symmetric routing (same path in both directions)
- Better resilience to high churn
- Flexible routing with multiple paths
- No periodic stabilization needed

**Disadvantages:**
- More complex routing algorithm
- Higher memory overhead (160 buckets vs m finger entries)
- Requires careful implementation of bucket eviction

## Performance Characteristics

### Lookup Performance
| Metric | Chord | Kademlia |
|--------|-------|----------|
| Hops | O(log n) | O(log n) |
| Latency | Sequential | Parallel (α queries) |
| Routing flexibility | Low (fixed path) | High (multiple paths) |
| Fault tolerance | Moderate | High |

### Network Overhead
| Operation | Chord | Kademlia |
|-----------|-------|----------|
| Join | O(log² n) messages | O(log n) messages |
| Maintenance | Periodic stabilization | On-demand updates |
| Storage per node | O(log n) | O(k × 160) ≈ O(1) for fixed k |

### Churn Resilience
- **Chord:** Requires stabilization to handle churn; slower convergence
- **Kademlia:** Handles churn naturally through parallel lookups and redundant routing paths

## Simulation Features

The simulation includes:
- Node join/leave operations
- Key-value storage and retrieval
- Hop count tracking for lookups
- Replication visualization
- Routing table state inspection

### Example Output
```
Kademlia network (k=20, alpha=3, nodes=5)
Nodes:
  Node: 41d5c82e8b13ea64... (ip=127.0.0.1, port=0)
    Routing table: 4 contacts
    Data (5 keys):
      file1.txt -> contents-1
      image.png -> png-bytes
      ...

Lookup simulation:
  From node 41d5c82e... -> file1.txt hops=0 found=yes
  From node 82827123... -> file1.txt hops=5 found=yes
  ...
```

## Testing and Validation

### Test Cases
1. **Basic Operations:** Node join, key storage, key retrieval
2. **Replication:** Verify keys stored on k closest nodes
3. **Lookup Routing:** Verify O(log n) hops
4. **Node Departure:** Verify data availability after node leaves
5. **Convergence:** Verify lookups converge to closest nodes

### Comparison Experiments
Run `./run_comparison.sh` to generate results for both protocols with identical workloads.

## Future Enhancements

Potential improvements for more realistic simulation:
1. Network latency simulation
2. Node failure and recovery
3. Bucket refresh implementation
4. Key republishing
5. Security features (secure node IDs)
6. Metrics collection (average hops, success rate, etc.)

## References

- [Kademlia Paper](https://pdos.csail.mit.edu/~petar/papers/maymounkov-kademlia-lncs.pdf) - Maymounkov & Mazières, 2002
- [Chord Paper](https://pdos.csail.mit.edu/papers/chord:sigcomm01/chord_sigcomm.pdf) - Stoica et al., 2001

## License

Part of CSEN 317 course project - comparison of DHT protocols.
