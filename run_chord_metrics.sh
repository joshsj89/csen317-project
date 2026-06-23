#!/bin/bash
# run_chords_metric_fixed.sh - improved metrics collection (ms timestamps, parse RPC replies)

M=8
BASE=12000
N=${1:-10}

mkdir -p logs
rm -f logs/*.log pids.txt metrics_join.txt metrics_lat.txt metrics_hops.txt metrics_fail.txt metrics_stabilization.txt metrics_requests.txt

KEYS=("alpha" "beta" "gamma" "delta" "epsilon")
VALUES=("one" "two" "three" "four" "five")

# helper: now() -> ms
now_ms() { date +%s%3N; }

echo "Launching $N Chord nodes..."

# Start first node
AUTORUN=1 ./chord_sim 127.0.0.1 $BASE $M > logs/node1.log 2>&1 &
echo $! > pids.txt
sleep 1

# Start remaining nodes + measure join time
for i in $(seq 2 $N); do
    port=$((BASE + i - 1))

    join_start=$(now_ms)

    AUTORUN=1 ./chord_sim 127.0.0.1 $port $M > logs/node$i.log 2>&1 &
    echo $! >> pids.txt
    sleep 1

    # send DOCKER_JOIN and capture response
    resp=$(printf "DOCKER_JOIN 127.0.0.1 $BASE\n" | nc 127.0.0.1 $port 2>/dev/null)
    join_end=$(now_ms)
    join_latency=$((join_end - join_start))

    # If resp empty, mark as timed out
    if [[ -z "$resp" ]]; then
        echo "$i ${join_latency} TIMEOUT" >> metrics_join.txt
        echo "JOIN node$i: ${join_latency} ms (no response)"
    else
        echo "$i ${join_latency} ${resp}" >> metrics_join.txt
        echo "JOIN node$i: ${join_latency} ms, resp='$resp'"
    fi
done

# Wait for stabilization (increase wait a bit)
echo "Waiting for stabilization..."
st_start=$(now_ms)

while true; do # check if all nodes report stable
    stable=true

    for i in $(seq 1 $N); do
        port=$((BASE + i - 1))
        resp=$(printf "IS_STABLE\n" | nc 127.0.0.1 $port 2>/dev/null)
        if [[ "$resp" != "OK 1" ]]; then
            stable=false
        fi
    done

    if $stable; then
        break
    fi

    sleep 0.25
done

st_end=$(now_ms)
stab_time=$((st_end - st_start))
echo "STABILIZATION_TIME $stab_time" > metrics_stabilization.txt
echo "Stabilization took $stab_time ms"

# Function to send a DOCKER_ RPC, return tuple "lat_ms|status|body"
do_rpc() {
    local port=$1
    local msg=$2
    local start=$(now_ms)
    # capture response (stdout)
    resp=$(printf "%s\n" "$msg" | nc 127.0.0.1 $port 2>/dev/null)
    local end=$(now_ms)
    local lat=$((end - start))
    if [[ -z "$resp" ]]; then
        echo "${lat}|TIMEOUT|"
    else
        # trim whitespace
        resp="$(echo "$resp" | tr -d '\r\n')"
        echo "${lat}|OK|${resp}"
    fi
}

# Collect put/get latencies and success
client_puts=0
client_gets=0

> metrics_lat.txt
> metrics_fail.txt

for i in "${!KEYS[@]}"; do
    key=${KEYS[$i]}
    val=${VALUES[$i]}

    p_put=$((BASE + (RANDOM % N)))
    res=$(do_rpc $p_put "DOCKER_PUT $key $val")
    IFS='|' read -r lat status body <<< "$res"
    echo "PUT $key $lat $status $body" >> metrics_lat.txt
    ((client_puts++))

    sleep 1

    p_get=$((BASE + (RANDOM % N)))
    res2=$(do_rpc $p_get "DOCKER_GET $key")
    IFS='|' read -r lat2 status2 body2 <<< "$res2"
    echo "GET $key $lat2 $status2 $body2" >> metrics_lat.txt
    ((client_gets++))

    # log failures
    if [[ "$status2" != "OK" ]]; then
        echo "GET_FAIL $key $status2 $body2" >> metrics_fail.txt
    fi
done

echo "PUT_REQUESTS $client_puts" > metrics_requests.txt
echo "GET_REQUESTS $client_gets" >> metrics_requests.txt

# Get metrics
total_calls=0
total_hops=0

> metrics_hops.txt

for i in $(seq 1 $N); do
    port=$((BASE + i - 1))

    metrics=$(printf "GET_METRICS\n" | nc 127.0.0.1 $port 2>/dev/null)

    if [[ $metrics == "OK "* ]]; then
        # expected: "OK calls hops"
        read _ calls hops <<< "$metrics"

        echo "Node$i: calls=$calls, hops=$hops" \
            >> metrics_hops.txt

        total_calls=$((total_calls + calls))
        total_hops=$((total_hops + hops))
    fi
done

echo "" >> metrics_hops.txt
echo "TOTAL_FIND_SUCCESSOR_CALLS $total_calls" >> metrics_hops.txt
echo "TOTAL_ROUTING_HOPS $total_hops" >> metrics_hops.txt

if [[ $total_calls -gt 0 ]]; then
    avg=$(awk "BEGIN {print $total_hops/$total_calls}")
    echo "AVG_HOPS_PER_LOOKUP $avg" >> metrics_hops.txt
fi

# Kill a random node
kill_node=$(( (RANDOM % N) + 1 )) # +1 because nodes are 1-indexed in pids.txt
echo "Killing node $kill_node"

pid=$(sed -n "${kill_node}p" pids.txt) # get pid of node to kill
kill -9 $pid
wait $pid 2>/dev/null # silence Bash messages
sed -i "${kill_node}d" pids.txt # remove from pids.txt

sleep 3

# Try GET on all keys and count failures
fails=0
total=0

> metrics_fail.txt

for key in "${KEYS[@]}"; do
    port=$((BASE + (RANDOM % N)))
    printf "DOCKER_GET $key\n" | nc 127.0.0.1 $port >/dev/null
    if [[ $? -ne 0 ]]; then
        fails=$((fails + 1))
    fi
    total=$((total + 1))
done

echo "GET_FAILURES $fails" >> metrics_fail.txt
echo "TOTAL_REQUESTS $total" >> metrics_fail.txt
echo "FAILURE_RATE $(awk "BEGIN {print $fails/$total*100}")%" >> metrics_fail.txt

# echo "DONE. Files produced:"
# ls -1 metrics_*.txt logs/*.log | sed -e 's/^/  /'

echo "Cleaning up: killing all remaining nodes..."
{ cat pids.txt | xargs kill -9; } 2>/dev/null # kill all remaining nodes and silence Bash messages
rm -f pids.txt

echo "All done. Metrics collected in metrics_*.txt, logs in logs/*.log"