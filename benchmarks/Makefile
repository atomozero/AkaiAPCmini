# Makefile for Haiku MIDI Performance Benchmarks
# Independent project for testing MIDI subsystem performance

# Project info
PROJECT_NAME = haiku-midi-benchmarks
VERSION = 1.0.0

# Directories
SRC_DIR = src
BUILD_DIR = build
RESULTS_DIR = results

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
CPPFLAGS = -I$(SRC_DIR)

# Haiku-specific flags and libraries
HAIKU_CXXFLAGS = -D_HAIKU_BUILD
HAIKU_LIBS = -lbe -lmidi2 -lmidi -lroot -ldevice

# Source files
VIRTUAL_MIDI_SRC = $(SRC_DIR)/virtual_midi_benchmark.cpp
MIDIKIT_DRIVER_SRC = $(SRC_DIR)/midikit_driver_test.cpp

# Binary targets
VIRTUAL_MIDI_BIN = virtual_midi_benchmark
MIDIKIT_DRIVER_BIN = midikit_driver_test

# Default target - build all benchmarks
.PHONY: all
all: $(VIRTUAL_MIDI_BIN) $(MIDIKIT_DRIVER_BIN)
	@echo ""
	@echo "=== All benchmarks built successfully ==="
	@echo "Run './run_benchmarks.sh' to execute all tests"

# Virtual MIDI benchmark (pure MidiKit routing, no hardware)
$(VIRTUAL_MIDI_BIN): $(VIRTUAL_MIDI_SRC)
	@echo "Building Virtual MIDI Benchmark..."
	$(CXX) $(CXXFLAGS) $(HAIKU_CXXFLAGS) $(CPPFLAGS) -o $@ $< $(HAIKU_LIBS)
	@echo "  -> Built: $(VIRTUAL_MIDI_BIN)"

# MidiKit driver test (tests midi_usb driver with hardware)
$(MIDIKIT_DRIVER_BIN): $(MIDIKIT_DRIVER_SRC)
	@echo "Building MidiKit Driver Test..."
	$(CXX) $(CXXFLAGS) $(HAIKU_CXXFLAGS) $(CPPFLAGS) -o $@ $< $(HAIKU_LIBS)
	@echo "  -> Built: $(MIDIKIT_DRIVER_BIN)"

# Individual build targets
.PHONY: virtual
virtual: $(VIRTUAL_MIDI_BIN)

.PHONY: driver
driver: $(MIDIKIT_DRIVER_BIN)

# Run benchmarks
.PHONY: run-virtual
run-virtual: $(VIRTUAL_MIDI_BIN)
	@echo "=== Running Virtual MIDI Benchmark ==="
	@./$(VIRTUAL_MIDI_BIN) | tee $(RESULTS_DIR)/virtual_midi_$(shell date +%Y%m%d_%H%M%S).txt

.PHONY: run-driver
run-driver: $(MIDIKIT_DRIVER_BIN)
	@echo "=== Running MidiKit Driver Test ==="
	@./$(MIDIKIT_DRIVER_BIN) | tee $(RESULTS_DIR)/midikit_driver_$(shell date +%Y%m%d_%H%M%S).txt

.PHONY: run-all
run-all: all
	@echo "=== Running All Benchmarks ==="
	@./scripts/run_all_benchmarks.sh

# Clean
.PHONY: clean
clean:
	@echo "Cleaning benchmark binaries..."
	@rm -f $(VIRTUAL_MIDI_BIN) $(MIDIKIT_DRIVER_BIN)
	@echo "Clean complete"

.PHONY: clean-results
clean-results:
	@echo "Cleaning result files..."
	@rm -f $(RESULTS_DIR)/*.txt
	@echo "Results cleaned"

.PHONY: distclean
distclean: clean clean-results
	@echo "Full clean complete"

# Help
.PHONY: help
help:
	@echo "Haiku MIDI Performance Benchmarks"
	@echo "=================================="
	@echo ""
	@echo "Build targets:"
	@echo "  make              - Build all benchmarks"
	@echo "  make virtual      - Build virtual MIDI benchmark only"
	@echo "  make driver       - Build MidiKit driver test only"
	@echo ""
	@echo "Run targets:"
	@echo "  make run-virtual  - Run virtual MIDI benchmark"
	@echo "  make run-driver   - Run MidiKit driver test (requires hardware)"
	@echo "  make run-all      - Run all benchmarks and save results"
	@echo ""
	@echo "Benchmarks:"
	@echo "  virtual_midi_benchmark  - Tests pure MidiKit routing (no hardware)"
	@echo "  midikit_driver_test     - Tests midi_usb driver with APC Mini"
	@echo ""
	@echo "Clean targets:"
	@echo "  make clean        - Remove binaries"
	@echo "  make clean-results - Remove result files"
	@echo "  make distclean    - Full clean (binaries + results)"
	@echo ""
	@echo "Results are saved to: $(RESULTS_DIR)/"
