# csen317-project

# Run the current Chord Protocol Simulation (currently nodes only, no file storage)
g++ -std=c++17 -o chord_sim chord_protocol.cpp
./chord_sim

# Outline (from our project proposal)

• Be sure to structure your plan into a set of incremental, implementable
milestones and include a milestone schedule
1. Implement basic Chord DHT node modules and hashing mechanism, as well as basic key adding/retrieving
2. Implement node joining/leaving mechanisms, ensuring consistent finger table updates
3. Conduct tests regarding stability and performance at varying scales, preferably at a large scale (entire point of the course)
4. Repeat steps 1-3 but for Kademlia DHT instead and implement each mechanism of Kademlia DHT that is different from Chord.
5. Compare the Methods
6. Presentation & Paper

# Current Progress
- Implemented basic Chord DHT node modules and hashing mechanism
- Implemented node joining/leaving mechanisms, ensuring consistent finger table updates

# Next Steps
- Implement basic key adding/retrieving for Chord DHT
- Conduct tests regarding stability and performance at varying scales