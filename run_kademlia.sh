#!/bin/bash

# Kademlia DHT Experiment Runner
# Compiles and runs the Kademlia simulation

echo "=================================="
echo "Kademlia DHT Simulation"
echo "=================================="
echo ""

# Compile
echo "Compiling Kademlia simulation..."
make kademlia_sim

if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

echo ""
echo "Running Kademlia simulation..."
echo "----------------------------------"
./kademlia_sim

echo ""
echo "=================================="
echo "Simulation complete!"
echo "=================================="
