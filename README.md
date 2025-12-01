# csen317-project

## Outline (from our project proposal)

Be sure to structure your plan into a set of incremental, implementable
milestones and include a milestone schedule

1. Implement basic Chord DHT node modules and hashing mechanism, as well as basic key adding/retrieving
2. Implement node joining/leaving mechanisms, ensuring consistent finger table updates
3. Conduct tests regarding stability and performance at varying scales, preferably at a large scale (entire point of the course)
4. Repeat steps 1-3 but for Kademlia DHT instead and implement each mechanism of Kademlia DHT that is different from Chord.
5. Compare the Methods
6. Presentation & Paper

## Current Progress

### Single Process
- Implemented basic Chord DHT node modules and hashing mechanism
- Implemented node joining/leaving mechanisms, ensuring consistent finger table updates
- Implemented basic key adding/retrieving for Chord DHT

### Multi Process
- Implemented basic Chord DHT node modules and hashing mechanism
- Implemented node joining/leaving mechanisms, ensuring consistent finger table updates
- Implemented basic key adding/retrieving for Chord DHT in multi-process setup

## Next Steps
- Implement key rebalancing in multi-process setup
- Implement key replication in multi-process setup
- Conduct tests regarding stability and performance at varying scales
- Implement Kademlia DHT mechanisms
- Compare Chord and Kademlia DHTs

## Run the current Chord Protocol Simulation (single-process)
```
g++ -std=c++17 -o chord_sim chord_protocol.cpp
./chord_sim
```

## Run the current Chord DHT Node (multi-process)
```
make clean all
./chord_sim <IP_ADDRESS> <PORT> <m bits>
```

Each node can be run in a separate terminal window. For example:
```
./chord_sim 127.0.0.1 5000 8
./chord_sim 127.0.0.1 5001 8
./chord_sim 127.0.0.1 5002 8
./chord_sim 127.0.0.1 5003 8
```

### Joining a node to an existing network
To join a node to an existing network, use the `join` command followed by the IP address and port of an existing node in the network. For example, to join the node running on 127.0.0.1:5000 from another node, enter:
```
join 127.0.0.1 5000
```

### Leaving a node from the network
To leave the network gracefully, use the `leave` command:
```
leave
```

### Crashing a node
To simulate a node crash, simply terminate the process (e.g., using Ctrl+C in the terminal) or use the `exit` command:
```
exit
```

### Retrieving node information
To retrieve information about the current node, use the `info` command:
```
info
```

### Adding a file (key-value pair)
To add a file (key-value pair) to the DHT, use the `put` command followed by the key and value. The value can contain spaces. For example:
```
put mykey myvalue
```

### Retrieving a file (value) by key
To retrieve a file (value) by key from the DHT, use the `get` command followed by the key. For example:
```
get mykey
```

### Sending RPC commands (for debugging/testing purposes only)
To send RPC commands to a node, you can use tools like `telnet` or `netcat`. For example, to send a `PING` command to a node running on 127.0.0.1:5000, use:
```
echo "PING" | nc 127.0.0.1 5000

echo "FIND_SUCCESSOR <key_id>" | nc 127.0.0.1 5000

echo "PUT <key> <value>" | nc 127.0.0.1 5000
```

## Docker Setup for Multi-Process Chord DHT Simulation
1. Build the Docker image:
   ```
   docker build -t chord-node .
   ```
2. Create a Docker network:
   ```
    docker network create chord-net
    ```
3. Run multiple Chord DHT nodes in separate Docker containers:
    ```
    docker run -it --rm --network chord-net --name node1 chord-node node1 12000 8
    docker run -it --rm --network chord-net --name node2 chord-node node2 12001 8
    docker run -it --rm --network chord-net --name node3 chord-node node3 12002 8
    docker run -it --rm --network chord-net --name node4 chord-node node4 12003 8
    ```
4. Join nodes to the network using the `join` command as described above.
    ```
    join node1 12000
    ```
