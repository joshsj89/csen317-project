#!/bin/bash
# run_chord.sh

M=8
BASE=12000
N=${1:-5} # Number of nodes, default to 5

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
    # echo "FIND_SUCCESSOR" | nc 127.0.0.1 $port >/dev/null

    echo "DOCKER_JOIN 127.0.0.1 $BASE" | nc 127.0.0.1 $port >/dev/null
done

echo "All nodes launched."
