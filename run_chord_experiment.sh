#!/bin/bash
# run_chord_experiment.sh

M=8
BASE=12000
N=${1:-5} # Number of nodes, default to 5

mkdir -p logs
rm -f logs/*.log
rm -f pids.txt latency_results.txt

KEYS=("alpha" "beta" "gamma" "delta" "epsilon")
VALUES=("one" "two" "three" "four" "five")

echo "Starting $N Chord nodes..."

# Start node1 (first node, no bootstrap)
AUTORUN=1 ./chord_sim 127.0.0.1 $BASE $M > logs/node1.log 2>&1 &
echo $! > pids.txt

sleep 1

# Start other nodes with bootstrap to node1
for i in $(seq 2 $N); do
    port=$((BASE + i - 1))

    AUTORUN=1 ./chord_sim 127.0.0.1 $port $M > logs/node$i.log 2>&1 &

    echo $! >> pids.txt

    # Have each node join manually via RPC
    sleep 1
    echo "DOCKER_JOIN 127.0.0.1 $BASE" | nc 127.0.0.1 $port >/dev/null
done

echo "All nodes launched. Waiting for stabilization..."
sleep 5

measure_latency() {
    local port=$1
    local msg=$2

    start_time=$(date +%s%N)
    # echo "$msg" | nc 127.0.0.1 $port >/dev/null
    printf "%s" "$msg" | nc -w 1 127.0.0.1 $port >/dev/null
    end_time=$(date +%s%N)
    latency=$((end_time - start_time))
    echo $latency
}

for i in "${!KEYS[@]}"; do
    k=${KEYS[$i]}
    v=${VALUES[$i]}

    # Random node for PUT
    p_put=$((BASE + (RANDOM % N)))
    put_msg="DOCKER_PUT $k $v\n"

    put_latency=$(measure_latency $p_put "$put_msg")
    echo "PUT $k on port $p_put: ${put_latency} ns"
    echo "PUT $put_latency" >> latency_results.txt

    sleep 1

    # Random node for GET
    p_get=$((BASE + (RANDOM % N)))
    get_msg="DOCKER_GET $k\n"

    get_latency=$(measure_latency $p_get "$get_msg")
    echo "GET $k on port $p_get: ${get_latency} ns"
    echo "GET $get_latency" >> latency_results.txt

    sleep 1
done

echo
echo "===== LATENCY SUMMARY ====="
awk '
{
    op=$1;
    val=$2+0;            # force float
    total[op]+=val;
    count[op]++
}
END {
    for (op in total) {
        avg_ms = total[op] / count[op] / 1000000.0;
        printf "%s avg = %.2f ms\n", op, avg_ms;
    }
}' latency_results.txt
echo

echo "Simulation complete."
echo "Use: cat pids.txt | xargs kill"