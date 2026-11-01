CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS  = -lncursesw

tetris: main.cpp data_structures.h
	$(CXX) $(CXXFLAGS) -o tetris main.cpp $(LDFLAGS)

clean:
	rm -f tetris highscores.dat

.PHONY: clean
