#!/bin/bash

# Run Kademlia performance experiment and save results

echo "=================================================="
echo "Kademlia DHT Performance Experiment"
echo "=================================================="
echo ""

# Compile
echo "Compiling..."
make kademlia_experiment

if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

echo ""
echo "Running experiment..."
echo "--------------------------------------------------"
./kademlia_experiment | tee kademlia_performance.txt

echo ""
echo "Results saved to kademlia_performance.txt"
echo "=================================================="
