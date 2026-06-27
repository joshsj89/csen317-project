# Test 5: Finger Table Verification
# Purpose: Verify the correctness of finger tables in the Chord ring. This catches
# incorrect successor entries and routing issues

BASE=12000
M=8
NUM_NODES=10

declare -a pids=()

die() {
    { pidof "chord_sim" | xargs kill -9; } 2>/dev/null
    echo -e "\033[31mTest 5 Failed: $1\033[0m" >&2
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
sleep 10

for node in $(seq 0 $((NUM_NODES - 1))); do
    port=$((BASE + node))
    fingers=$(printf "GET_FINGERS\n" | nc 127.0.0.1 $port)
    
    while read -r start finger_id ip finger_port; do
        # Calculate expected finger_id
        expected=$(printf "FIND_SUCCESSOR $start\n" | nc 127.0.0.1 $BASE | awk '{print $2}')

        if [[ "$finger_id" != "$expected" ]]; then
            die "Node $node: Finger $start has ID $finger_id, expected $expected"
        fi
    done < <(echo "$fingers" | tail -n +2) # Skip the first line which is a header
done

# If we reach this point, the test passed
echo -e "\033[32mTest 5 Passed: Finger Table Verification works correctly.\033[0m"

# Kill all launched nodes
for pid in "${pids[@]}"; do
    kill -9 $pid 2>/dev/null
    wait $pid 2>/dev/null
done

exit 0