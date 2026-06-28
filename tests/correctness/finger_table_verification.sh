# Test 5: Finger Table Verification
# Purpose: Verify the correctness of finger tables in the Chord ring. This catches
# incorrect successor entries and routing issues

BASE=12000
M=8
NUM_NODES=10

source ./utils.sh

declare -a pids=()

# Launch nodes
launch_nodes $BASE $M $NUM_NODES pids

# Wait for stabilization
sleep 10

for node in $(seq 0 $((NUM_NODES - 1))); do
    port=$((BASE + node))
    fingers=$(printf "GET_FINGERS\n" | nc 127.0.0.1 $port)
    
    while read -r start finger_id ip finger_port; do
        # Calculate expected finger_id
        expected=$(printf "FIND_SUCCESSOR $start\n" | nc 127.0.0.1 $BASE | awk '{print $2}')

        if [[ "$finger_id" != "$expected" ]]; then
            die 5 "Node $node: Finger $start has ID $finger_id, expected $expected"
        fi
    done < <(echo "$fingers" | tail -n +2) # Skip the first line which is a header
done

# If we reach this point, the test passed
echo -e "\033[32mTest 5 Passed: Finger Table Verification works correctly.\033[0m"

# Kill all launched nodes
kill_nodes pids

exit 0