# Test 6: Ring Consistency
# Purpose: Verify that every node forms one correct ring

BASE=12000
M=8
NUM_NODES=10

declare -a pids=()

die() {
    { pidof "chord_sim" | xargs kill -9; } 2>/dev/null
    echo -e "\033[31mTest 6 Failed: $1\033[0m" >&2
    exit 1
}

# Launch bootstrap node
AUTORUN=1 ../chord_sim 127.0.0.1 $BASE $M 2>&1 &
pids+=($!)

# Launch remaining nodes
for i in $(seq 1 $((NUM_NODES - 1))); do
    port=$((BASE + i))
    AUTORUN=1 ../chord_sim 127.0.0.1 $port $M 2>&1 &
    pids+=($!)
    sleep 1

    # Join node to bootstrap node
    printf "DOCKER_JOIN 127.0.0.1 $BASE\n" | nc 127.0.0.1 $port 2>/dev/null
done

# Wait for stabilization
sleep 2

# Verify that each node has the correct successor and predecessor

# Collect Node ID, Predecessor ID, and Successor ID for each node in associative arrays
declare -A node_ids
declare -A predecessors
declare -A successors

for node in $(seq 0 $((NUM_NODES - 1))); do
    port=$((BASE + node))
    ring_info=$(printf "GET_RING\n" | nc 127.0.0.1 $port)
    predecessor_id=$(echo "$ring_info" | awk '{print $2}')
    node_id=$(echo "$ring_info" | awk '{print $3}')
    successor_id=$(echo "$ring_info" | awk '{print $4}')

    node_ids[$node]=$node_id
    predecessors[$node]=$predecessor_id
    successors[$node]=$successor_id

    # Ask successor for GET_RING and verify that it matches the current node's ID
    successor_info=$(printf "GET_SUCCESSOR\n" | nc 127.0.0.1 $port)
    successor_port=$(echo "$successor_info" | awk '{print $4}')

    successor_ring_info=$(printf "GET_RING\n" | nc 127.0.0.1 $successor_port)
    successor_predecessor_id=$(echo "$successor_ring_info" | awk '{print $2}')

    if [[ "$successor_predecessor_id" != "${node_ids[$node]}" ]]; then
        die "Node $node's successor's predecessor ID ($successor_predecessor_id) does not match Node $node's ID (${node_ids[$node]})"
    fi

    # Ask predecessor for GET_RING and verify that it matches the current node's ID
    predecessor_info=$(printf "GET_PREDECESSOR\n" | nc 127.0.0.1 $port)
    predecessor_port=$(echo "$predecessor_info" | awk '{print $4}')

    predecessor_ring_info=$(printf "GET_RING\n" | nc 127.0.0.1 $predecessor_port)
    predecessor_successor_id=$(echo "$predecessor_ring_info" | awk '{print $4}')

    if [[ "$predecessor_successor_id" != "${node_ids[$node]}" ]]; then
        die "Node $node's predecessor's successor ID ($predecessor_successor_id) does not match Node $node's ID (${node_ids[$node]})"
    fi
done

# Walk the ring starting from the bootstrap node and ensure we can visit all nodes exactly once
visited_nodes=()
current_node=$BASE
for i in $(seq 0 $((NUM_NODES - 1))); do
    successor_info=$(printf "GET_SUCCESSOR\n" | nc 127.0.0.1 $current_node)
    current_node=$(echo "$successor_info" | awk '{print $4}')

    if [[ " ${visited_nodes[@]} " =~ " ${current_node} " ]]; then
        die "Node $current_node has already been visited. Ring is inconsistent."
    fi

    visited_nodes+=($current_node)
done

if [[ ${#visited_nodes[@]} -ne $NUM_NODES ]]; then
    die "Not all nodes were visited in the ring walk. Ring is inconsistent."
fi

if [[ "$current_node" != "$BASE" ]]; then
    die "Ring walk did not return to the bootstrap node. Ring is inconsistent."
fi

# If we reach this point, the test passed
echo -e "\033[32mTest 6 Passed: Ring Consistency Verification works correctly.\033[0m"

# Kill all launched nodes
for pid in "${pids[@]}"; do
    kill -9 $pid 2>/dev/null
    wait $pid 2>/dev/null
done

exit 0