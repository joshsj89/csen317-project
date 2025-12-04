#!/bin/bash

# Chord vs Kademlia Comparison Experiment
# Runs both DHT simulations and compares results

echo "================================================"
echo "DHT Comparison: Chord vs Kademlia"
echo "================================================"
echo ""

# Clean previous builds
echo "Cleaning previous builds..."
make clean

# Compile both
echo ""
echo "Compiling Chord DHT..."
make chord_sim

if [ $? -ne 0 ]; then
    echo "Chord compilation failed!"
    exit 1
fi

echo ""
echo "Compiling Kademlia DHT..."
make kademlia_sim

if [ $? -ne 0 ]; then
    echo "Kademlia compilation failed!"
    exit 1
fi

# Run Chord
echo ""
echo "================================================"
echo "Running Chord DHT Simulation"
echo "================================================"
./chord_sim > chord_results.txt
echo "Chord results saved to chord_results.txt"

# Run Kademlia
echo ""
echo "================================================"
echo "Running Kademlia DHT Simulation"
echo "================================================"
./kademlia_sim > kademlia_results.txt
echo "Kademlia results saved to kademlia_results.txt"

echo ""
echo "================================================"
echo "Comparison Summary"
echo "================================================"
echo ""
echo "Results saved to:"
echo "  - chord_results.txt"
echo "  - kademlia_results.txt"
echo ""
echo "Key Differences:"
echo ""
echo "CHORD DHT:"
echo "  - Ring-based topology with consistent hashing"
echo "  - Each node maintains O(log n) finger table entries"
echo "  - Lookups: O(log n) hops"
echo "  - Uses successor pointers and finger tables"
echo "  - Stabilization protocol for maintenance"
echo ""
echo "KADEMLIA DHT:"
echo "  - XOR metric for distance"
echo "  - k-bucket routing tables (k=20 typically)"
echo "  - Lookups: O(log n) hops with alpha-parallel queries"
echo "  - Symmetric routing (same path in both directions)"
echo "  - Better resilience to churn and node failures"
echo "  - Parallel lookups improve latency"
echo ""
echo "Performance Considerations:"
echo "  - Kademlia's XOR metric provides flexible routing"
echo "  - Kademlia's parallel queries can reduce latency"
echo "  - Chord has simpler structure and maintenance"
echo "  - Kademlia handles high churn better"
echo ""
echo "================================================"
