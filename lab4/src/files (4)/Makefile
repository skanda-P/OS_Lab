CXX      ?= g++
CXXFLAGS ?= -O3 -march=native -std=c++17 -Wall -Wextra

memsim: memsim.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f memsim

.PHONY: clean
