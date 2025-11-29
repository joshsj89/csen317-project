# csen317-project

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

- Implemented basic Chord DHT node modules and hashing mechanism
- Implemented node joining/leaving mechanisms, ensuring consistent finger table updates
- Implemented basic key adding/retrieving for Chord DHT

## Next Steps

- Conduct tests regarding stability and performance at varying scales
- Implement Kademlia DHT mechanisms
- Compare Chord and Kademlia DHTs
