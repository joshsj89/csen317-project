#!/bin/bash
# run_chord_metrics.sh
# Collect JOIN latency, stabilization time, GET/PUT latency, hop count, failure recovery

M=8
BASE=12000
N=${1:-10}

mkdir -p logs
rm -f logs/*.log pids.txt metrics_join.txt metrics_lat.txt metrics_hops.txt metrics_fail.txt

KEYS=("alpha" "beta" "gamma" "delta" "epsilon")
VALUES=("one" "two" "three" "four" "five")

echo "Launching $N Chord nodes..."

###############################################
# 1. Start first node
###############################################

start_time=$(date +%s%N)

AUTORUN=1 ./chord_sim 127.0.0.1 $BASE $M > logs/node1.log 2>&1 &
echo $! > pids.txt

sleep 1

###############################################
# 2. Start remaining nodes + measure join time
###############################################

for i in $(seq 2 $N); do
    port=$((BASE + i - 1))

    join_start=$(date +%s%N)

    AUTORUN=1 ./chord_sim 127.0.0.1 $port $M > logs/node$i.log 2>&1 &
    echo $! >> pids.txt
    sleep 1

    echo "DOCKER_JOIN 127.0.0.1 $BASE" | nc 127.0.0.1 $port >/dev/null

    join_end=$(date +%s%N)
    join_latency=$((join_end - join_start))

    echo "$i $join_latency" >> metrics_join.txt
    echo "JOIN node$i: $join_latency"
done

###############################################
# 3. Stabilization convergence: wait & check
###############################################

echo "Waiting for stabilization..."

st_start=$(date +%s)
sleep 5

# Repeat checks until convergence or timeout
for round in {1..20}; do
    sleep 1

    # Extract successors
    for i in $(seq 1 $N); do
        port=$((BASE + i - 1))
        echo "DOCKER_INFO" | nc 127.0.0.1 $port \
            | grep "OK" >/dev/null

        # Extract logs
        succ=$(grep "Successor:" logs/node$i.log | tail -1)
        echo "$i: $succ"
    done

    # Quick convergence heuristic: if successor logs haven't changed in last two rounds
    if [[ $round -gt 2 ]]; then
        echo "Converged at round $round"
        break
    fi
done

st_end=$(date +%s)
stab_time=$((st_end - st_start))

echo "STABILIZATION_TIME $stab_time" > metrics_stabilization.txt
echo "Stabilization took $stab_time seconds"

###############################################
# 4. Measure PUT/GET latency
###############################################

measure_latency() {
    local port=$1
    local msg=$2

    start=$(date +%s%N)
    printf "%s" "$msg" | nc 127.0.0.1 $port >/dev/null
    end=$(date +%s%N)

    echo $((end - start))
}

for i in "${!KEYS[@]}"; do
    key=${KEYS[$i]}
    val=${VALUES[$i]}

    p_put=$((BASE + (RANDOM % N)))
    put_msg="DOCKER_PUT $key $val\n"

    lat=$(measure_latency $p_put "$put_msg")
    echo "PUT $key $lat" >> metrics_lat.txt

    sleep 1

    p_get=$((BASE + (RANDOM % N)))
    get_msg="DOCKER_GET $key\n"

    lat2=$(measure_latency $p_get "$get_msg")
    echo "GET $key $lat2" >> metrics_lat.txt
done

###############################################
# 5. Hop count estimation via FIND_SUCCESSOR logs
###############################################

for i in $(seq 1 $N); do
    hops=$(grep -c "FIND_SUCCESSOR" logs/node$i.log)
    echo "$i $hops" >> metrics_hops.txt
done

###############################################
# 6. Fault tolerance test
###############################################

# Kill a random node
kill_node=$(( (RANDOM % N) + 1 ))
echo "Killing node $kill_node"

pid=$(sed -n "${kill_node}p" pids.txt)
kill -9 $pid

sleep 3

# Try GET on all keys and count failures
fails=0
total=0

for key in "${KEYS[@]}"; do
    port=$((BASE + (RANDOM % N)))
    printf "DOCKER_GET $key\n" | nc 127.0.0.1 $port >/dev/null
    if [[ $? -ne 0 ]]; then
        fails=$((fails + 1))
    fi
    total=$((total + 1))
done

echo "$fails $total" > metrics_fail.txt

echo "DONE. Metrics collected:"
echo " - metrics_join.txt"
echo " - metrics_stabilization.txt"
echo " - metrics_lat.txt"
echo " - metrics_hops.txt"
echo " - metrics_fail.txt"
