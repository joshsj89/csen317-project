die() {
    { pidof "chord_sim" | xargs kill -9; } 2>/dev/null
    echo -e "\033[31mTest $1 Failed: $2\033[0m" >&2
    exit 1
}

# Launch nodes and store their PIDs in an array
launch_nodes() {
    local BASE=$1
    local M=$2
    local NUM_NODES=$3
    local -n pids_arr=$4

    # Launch bootstrap node
    AUTORUN=1 ../chord_sim 127.0.0.1 $BASE $M 2>&1 &
    pids_arr+=($!)

    # Launch remaining nodes
    for i in $(seq 1 $((NUM_NODES - 1))); do
        port=$((BASE + i))
        AUTORUN=1 ../chord_sim 127.0.0.1 $port $M 2>&1 &
        pids_arr+=($!)
        sleep 1

        # Join node to bootstrap node
        printf "DOCKER_JOIN 127.0.0.1 $BASE\n" | nc 127.0.0.1 $port 2>/dev/null
    done
}

# Kill all launched nodes
kill_nodes() {
    local -n pids_arr=$1
    for pid in "${pids_arr[@]}"; do
        kill -9 $pid 2>/dev/null
        wait $pid 2>/dev/null
    done
}