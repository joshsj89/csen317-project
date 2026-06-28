# Test 3: Multi-Node Routing
# Purpose: Routing works.

BASE=12000
M=8
NUM_NODES=10
NUM_KEYS=255

source ./utils.sh

declare -a pids=()

# Launch nodes
launch_nodes $BASE $M $NUM_NODES pids

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
sleep 2

# Retrieve every key from all nodes and check if they are correct
successful_retrievals=0
for i in $(seq 1 $NUM_KEYS); do
    key="key$i"
    expected_value="value$i"

    # Try to retrieve from each node
    for j in $(seq 0 $((NUM_NODES - 1))); do
        port=$((BASE + j))
        actual_value=$(printf "DOCKER_GET $key\n" | nc 127.0.0.1 $port | awk '{print $2}')
        if [[ "$actual_value" != "$expected_value" ]]; then
            die 3 "Node $j expected $expected_value for $key, but got $actual_value"
        else
            ((successful_retrievals++))
        fi
    done
done

if [[ $successful_retrievals -ne $((NUM_KEYS * NUM_NODES)) ]]; then
    die 3 "Not all keys were successfully retrieved from all nodes."
fi

# If we reach this point, the test passed
echo -e "\033[32mTest 3 Passed: Multi-Node Routing works correctly.\033[0m"

# Kill all launched nodes
kill_nodes pids

exit 0