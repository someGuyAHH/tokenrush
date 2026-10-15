# Linux / macOS (Windows: use build.py)
#   make            tokenrush-tui
#   make sim        the headless balance simulator (tools/sim.cpp)
# The TUI needs dependencies/ftxui.hpp and dependencies/ftxui.cpp (FTXUI 7.0.3, amalgamated).
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pthread
CPPFLAGS += -Isrc

CORE := $(wildcard src/core/*.cpp)
SHELL_SRC := $(wildcard src/shell/*.cpp)
TUI  := $(wildcard src/tui/*.cpp)
HDRS := $(wildcard src/core/*.hpp src/shell/*.hpp src/tui/*.hpp)
OBJ   = $(patsubst src/%.cpp,build/%.o,$(1))

all: tokenrush-tui

tokenrush-tui: $(call OBJ,$(CORE) $(SHELL_SRC) $(TUI)) build/ftxui.o
	$(CXX) $(CXXFLAGS) $^ -o $@

sim: $(call OBJ,$(CORE)) build/tools/sim.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/tui/%.o: src/tui/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Idependencies -c $< -o $@

build/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

build/ftxui.o: dependencies/ftxui.cpp dependencies/ftxui.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/tools/%.o: tools/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf build tokenrush-tui sim

.PHONY: all clean
