CXX 		= g++ -std=c++17
CXXFLAGS 	= -g -Wall -Wextra
EXTRAS		= chord.cpp node.cpp rpc.cpp utils.cpp
TARGET		= chord_sim


all: 		$(TARGET)

$(TARGET): 	$(EXTRAS)
		$(CXX) $(CXXFLAGS) -o $(TARGET) $(EXTRAS)

clean:
		$(RM) $(TARGET) *.out