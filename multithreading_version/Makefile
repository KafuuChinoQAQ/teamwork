CXX ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?= -pthread

OBJS = main.o elf.o cfg.o

all: cfgtool

cfgtool: $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

main.o: src/main.cpp src/cfgtool.h
	$(CXX) $(CXXFLAGS) -c src/main.cpp -o $@

elf.o: src/elf.cpp src/cfgtool.h
	$(CXX) $(CXXFLAGS) -c src/elf.cpp -o $@

cfg.o: src/cfg.cpp src/cfgtool.h
	$(CXX) $(CXXFLAGS) -c src/cfg.cpp -o $@

clean:
	rm -f cfgtool $(OBJS)
