#!/bin/bash
# run_kademlia_metrics.sh
# Metrics collection for the Kademlia simulator

N=${1:-10}

mkdir -p logs
rm -f metrics_join.txt metrics_lat.txt metrics_hops.txt metrics_fail.txt metrics_stabilization.txt

# helper: now() -> ms
now_ms() { date +%s%3N; }

echo "=== Starting Kademlia Network with $N nodes ==="

###############################################
# 1. Launch nodes and measure join time
###############################################

echo "Adding nodes..."
> metrics_join.txt
NODE_IDS=()

for i in $(seq 1 $N); do
    name="node-$i"

    join_start=$(now_ms)
    id=$(./kademlia_sim ADD_NODE "$name" 2>/dev/null)
    join_end=$(now_ms)
    latency=$((join_end - join_start))

    NODE_IDS+=("$id")

    echo "$i $latency $id" >> metrics_join.txt
    echo "Node $i joined in ${latency}ms (ID=$id)"
done

###############################################
# 2. Stabilization wait
###############################################

echo "Waiting for stabilization (Kademlia is fast)..."
st_start=$(date +%s)
sleep 1   # Kademlia does not need stabilization, but we wait 1s
st_end=$(date +%s)
stab=$((st_end - st_start))

echo "STABILIZATION_TIME $stab" > metrics_stabilization.txt
echo "Stabilization: $stab seconds"

###############################################
# 3. PUT / GET latency
###############################################

KEYS=("alpha" "beta" "gamma" "delta" "epsilon")
VALUES=("one" "two" "three" "four" "five")

> metrics_lat.txt
echo "Measuring PUT/GET..."

for i in "${!KEYS[@]}"; do
    key=${KEYS[$i]}
    val=${VALUES[$i]}

    # PUT timing
    put_start=$(now_ms)
    put_resp=$(./kademlia_sim PUT "$key" "$val" 2>/dev/null)
    put_end=$(now_ms)
    put_lat=$((put_end - put_start))
    echo "PUT $key $put_lat $put_resp" >> metrics_lat.txt
    echo "PUT $key: ${put_lat}ms"

    sleep 0.5

    # GET timing
    get_start=$(now_ms)
    get_resp=$(./kademlia_sim GET "$key" 2>/dev/null)
    get_end=$(now_ms)
    get_lat=$((get_end - get_start))
    echo "GET $key $get_lat $get_resp" >> metrics_lat.txt
    echo "GET $key: ${get_lat}ms"

    sleep 0.5
done

###############################################
# 4. Hop-count measurements
###############################################

> metrics_hops.txt
echo "Hop counts..."

for id in "${NODE_IDS[@]}"; do
    for key in "${KEYS[@]}"; do
        hops=$(./kademlia_sim LOOKUP "$id" "$key" 2>/dev/null)
        echo "$id $key $hops" >> metrics_hops.txt
    done
done

###############################################
# 5. Failure test (remove a node and test GET)
###############################################

kill_index=$(( (RANDOM % N) + 1 ))
kill_id=${NODE_IDS[$kill_index-1]}

echo "Removing node $kill_id" | tee -a metrics_fail.txt
./kademlia_sim REMOVE_NODE "$kill_id"

fails=0
total=0

for key in "${KEYS[@]}"; do
    resp=$(./kademlia_sim GET "$key" 2>/dev/null)
    if [[ -z "$resp" ]]; then
        fails=$((fails + 1))
    fi
    total=$((total + 1))
done

echo "$fails $total" >> metrics_fail.txt
echo "Failures: $fails / $total"

###############################################
# DONE
###############################################

echo
echo "=== METRICS COMPLETE ==="
ls -1 metrics_*.txt | sed 's/^/  /'
