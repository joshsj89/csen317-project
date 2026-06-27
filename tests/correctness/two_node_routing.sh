# Test 2: Two-Node Routing
# Purpose: Routing works.

BASE=12000
M=8
NUM_KEYS=200

die() {
    { pidof "chord_sim" | xargs kill -9; } 2>/dev/null
    echo -e "\033[31mTest 2 Failed: $1\033[0m" >&2
    exit 1
}

# Launch Node A
AUTORUN=1 ../chord_sim 127.0.0.1 $BASE $M 2>&1 &
pidA=$!

# Launch Node B
AUTORUN=1 ../chord_sim 127.0.0.1 $((BASE + 1)) $M 2>&1 &
pidB=$!

# Join Node B to Node A
printf "DOCKER_JOIN 127.0.0.1 $BASE\n" | nc 127.0.0.1 $((BASE + 1)) 2>/dev/null

# Wait for stabilization
sleep 2

# Insert $NUM_KEYS keys into random node (A or B)
for i in $(seq 1 $NUM_KEYS); do
    key="key$i"
    value="value$i"
    # Randomly choose between Node A and Node B
    if (( RANDOM % 2 == 0 )); then
        printf "PUT $key $value\n" | nc 127.0.0.1 $BASE >/dev/null 2>&1
    else
        printf "PUT $key $value\n" | nc 127.0.0.1 $((BASE + 1)) >/dev/null 2>&1
    fi
done

# Wait a bit to ensure all keys are propagated
sleep 1

# Retrieve every key from both nodes and check if they are correct
successful_retrievals=0
for i in $(seq 1 $NUM_KEYS); do
    key="key$i"
    expected_value="value$i"

    # Try to retrieve from Node A first
    actual_value=$(printf "DOCKER_GET $key\n" | nc 127.0.0.1 $BASE | awk '{print $2}')
    if [[ "$actual_value" != "$expected_value" ]]; then
        die "Node A expected $expected_value for $key, but got $actual_value"
    else
        ((successful_retrievals++))
    fi

    # Try to retrieve from Node B
    actual_value=$(printf "DOCKER_GET $key\n" | nc 127.0.0.1 $((BASE + 1)) | awk '{print $2}')
    if [[ "$actual_value" != "$expected_value" ]]; then
        die "Node B expected $expected_value for $key, but got $actual_value"
    else
        ((successful_retrievals++))
    fi
done

if [[ $successful_retrievals -ne $((NUM_KEYS * 2)) ]]; then
    die "Not all keys were successfully retrieved from both nodes."
fi

# If we reach this point, the test passed
echo -e "\033[32mTest 2 Passed: Two-Node Routing works correctly.\033[0m"
kill -9 $pidA
wait $pidA 2>/dev/null
kill -9 $pidB
wait $pidB 2>/dev/null
exit 0