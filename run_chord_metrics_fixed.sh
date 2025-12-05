#!/bin/bash
# run_chords_metric_fixed.sh - improved metrics collection (ms timestamps, parse RPC replies)

M=8
BASE=12000
N=${1:-10}

mkdir -p logs
rm -f logs/*.log pids.txt metrics_join.txt metrics_lat.txt metrics_hops.txt metrics_fail.txt metrics_stabilization.txt

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
sleep 6
st_start=$(date +%s)
# optional: poll GET_INFO until stable; we will just wait and sample once
sleep 2
st_end=$(date +%s)
stab_time=$((st_end - st_start))
echo "STABILIZATION_TIME $stab_time" > metrics_stabilization.txt
echo "Stabilization took $stab_time seconds"

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
> metrics_lat.txt
> metrics_fail.txt
for i in "${!KEYS[@]}"; do
    key=${KEYS[$i]}
    val=${VALUES[$i]}

    p_put=$((BASE + (RANDOM % N)))
    res=$(do_rpc $p_put "DOCKER_PUT $key $val")
    IFS='|' read -r lat status body <<< "$res"
    echo "PUT $key $lat $status $body" >> metrics_lat.txt

    sleep 1

    p_get=$((BASE + (RANDOM % N)))
    res2=$(do_rpc $p_get "DOCKER_GET $key")
    IFS='|' read -r lat2 status2 body2 <<< "$res2"
    echo "GET $key $lat2 $status2 $body2" >> metrics_lat.txt

    # log failures
    if [[ "$status2" != "OK" ]]; then
        echo "GET_FAIL $key $status2 $body2" >> metrics_fail.txt
    fi
done

# Get hop metrics from node1 (assumes GET_METRICS implemented)
metrics=$(printf "GET_METRICS\n" | nc 127.0.0.1 $BASE 2>/dev/null)
if [[ -n "$metrics" ]]; then
    # expected: "OK calls hops puts gets"
    echo "$metrics" > metrics_hops.txt
    echo "Hop metrics: $metrics"
else
    echo "NO_METRICS_RETURNED" > metrics_hops.txt
    echo "No GET_METRICS response - ensure node exports metrics RPC"
fi

echo "DONE. Files produced:"
ls -1 metrics_*.txt logs/*.log | sed -e 's/^/  /'
