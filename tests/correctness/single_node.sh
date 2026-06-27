# Test 1: Single Node
# Purpose: One node should own every key.

BASE=12000
M=8
NUM_KEYS=100

die() {
    { pidof "chord_sim" | xargs kill -9; } 2>/dev/null
    echo -e "\033[31mTest 1 Failed: $1\033[0m" >&2
    exit 1
}

# Launch Node
AUTORUN=1 ../chord_sim 127.0.0.1 $BASE $M 2>&1 &
pid=$!

# Wait for stabilization
sleep 2

# Insert $NUM_KEYS keys
for i in $(seq 1 $NUM_KEYS); do
    key="key$i"
    value="value$i"
    printf "PUT $key $value\n" | nc 127.0.0.1 $BASE >/dev/null 2>&1
done

# Retrieve $NUM_KEYS keys and check if they are correct
for i in $(seq 1 $NUM_KEYS); do
    key="key$i"
    expected_value="value$i"
    actual_value=$(printf "DOCKER_GET $key\n" | nc 127.0.0.1 $BASE | awk '{print $2}')
    if [[ "$actual_value" != "$expected_value" ]]; then
        die "Expected $expected_value for $key, but got $actual_value"
    fi
done

# Verify LIST_KEYS returns $NUM_KEYS keys and each says it's a primary key (0 replicas)
list_keys=$(printf "LIST_KEYS\n" | nc 127.0.0.1 $BASE)
key_count=$(echo "$list_keys" | grep -c "key[0-9]\+")
if [[ $key_count -ne $NUM_KEYS ]]; then
    die "Expected $NUM_KEYS keys, but got $key_count"
fi

# Check that each key is marked as primary
for i in $(seq 1 $NUM_KEYS); do
    key="key$i"
    if ! echo "$list_keys" | grep -q "$key primary"; then
        die "$key is not marked as primary"
    fi
done

# If we reach this point, the test passed
echo -e "\033[32mTest 1 Passed: Single Node owns all keys and they are marked as primary.\033[0m"
kill -9 $pid
wait $pid 2>/dev/null
exit 0