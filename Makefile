CXX 		= g++ -std=c++20
CXXFLAGS 	= -g -Wall -Wextra
CHORD_EXTRAS	= chord.cpp node.cpp rpc.cpp utils.cpp
KADEMLIA_EXTRAS	= kademlia.cpp
CHORD_TARGET	= chord_sim
KADEMLIA_TARGET	= kademlia_sim
KADEMLIA_EXP	= kademlia_experiment


all: 		$(CHORD_TARGET) $(KADEMLIA_TARGET) $(KADEMLIA_EXP)

$(CHORD_TARGET): $(CHORD_EXTRAS)
		$(CXX) $(CXXFLAGS) -o $(CHORD_TARGET) $(CHORD_EXTRAS)

$(KADEMLIA_TARGET): $(KADEMLIA_EXTRAS) kademlia_protocol.cpp kademlia_sim.cpp
		$(CXX) $(CXXFLAGS) -o $(KADEMLIA_TARGET) $(KADEMLIA_EXTRAS) kademlia_protocol.cpp

$(KADEMLIA_EXP): $(KADEMLIA_EXTRAS) kademlia_experiment.cpp
		$(CXX) $(CXXFLAGS) -o $(KADEMLIA_EXP) $(KADEMLIA_EXTRAS) kademlia_experiment.cpp

clean:
		$(RM) $(CHORD_TARGET) $(KADEMLIA_TARGET) $(KADEMLIA_EXP) *.out *.txt