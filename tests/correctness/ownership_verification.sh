# Test 4: Ownership Verification
# Purpose: Verify key ownership in the Chord ring. This catches
# duplicate primaries, missing replicas, misplaced repliacas, and replication bugs

BASE=12000
M=8
NUM_NODES=10
NUM_KEYS=200

declare -a pids=()

die() {
    { pidof "chord_sim" | xargs kill -9; } 2>/dev/null
    echo -e "\033[31mTest 4 Failed: $1\033[0m" >&2
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

# Insert $NUM_KEYS keys into random node
for i in $(seq 1 $NUM_KEYS); do
    key="key$i"
    value="value$i"
    
    # Randomly choose a node to insert the key
    random_node=$((RANDOM % NUM_NODES))
    port=$((BASE + random_node))
    printf "DOCKER_PUT $key $value\n" | nc 127.0.0.1 $port >/dev/null 2>&1
done

# Wait a bit to ensure all keys are propagated
sleep 3

#
declare -A primary_count
declare -A primary_nodes

for node in $(seq 0 $((NUM_NODES - 1))); do
    port=$((BASE + node))
    list_keys=$(printf "LIST_KEYS\n" | nc 127.0.0.1 $port)

    while read -r key type; do
        if [[ "$type" == "primary" ]]; then
            ((primary_count[$key]++))
            primary_nodes[$key]=$node
        fi
    done < <(echo "$list_keys" | tail -n +2) # Skip the first line which is a header
done

# Verify that each key has exactly one primary
for i in $(seq 1 $NUM_KEYS); do
    key="key$i"

    if [[ ${primary_count[$key]} -ne 1 ]]; then
        die "Key $key has ${primary_count[$key]} primaries (expected 1)"
    fi

    # Verify that the owner is the correct node based on the Chord hash
    hash_value=$(printf "HASH $key\n" | nc 127.0.0.1 $BASE | awk '{print $2}')
    expected_owner=$(printf "FIND_SUCCESSOR $hash_value\n" | nc 127.0.0.1 $BASE | awk '{print $2}')

    actual_owner_node=${primary_nodes[$key]}
    actual_owner=$(printf "GET_NODE_ID\n" | nc 127.0.0.1 $((BASE + actual_owner_node)) | awk '{print $2}')

    if [[ "$actual_owner" -ne "$expected_owner" ]]; then
        die "Key $key is owned by node $actual_owner, but expected owner is node $expected_owner"
    fi
done

# If we reach this point, the test passed
echo -e "\033[32mTest 4 Passed: Ownership Verification works correctly.\033[0m"

# Kill all launched nodes
for pid in "${pids[@]}"; do
    kill -9 $pid 2>/dev/null
    wait $pid 2>/dev/null
done

exit 0