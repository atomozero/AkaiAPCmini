# Makefile for APC Mini Test Application - Haiku OS
# Compatible with GCC on Haiku R1/beta5+

# Application settings
APP_NAME = apc_mini_test
APP_VERSION = 1.0.0

# Directories
SRC_DIR = ../src
BUILD_DIR = .
OBJ_DIR = obj
EXAMPLES_DIR = ../examples

# Source files
SOURCES = $(SRC_DIR)/apc_mini_test.cpp \
          $(SRC_DIR)/usb_haiku_midi.cpp

# GUI application sources
GUI_SOURCES = $(SRC_DIR)/apc_mini_gui.cpp \
              $(SRC_DIR)/apc_mini_gui_app.cpp \
              $(SRC_DIR)/apc_mini_gui_main.cpp \
              $(SRC_DIR)/apc_mini_debug_log.cpp \
              $(SRC_DIR)/usb_haiku_midi.cpp \
              $(SRC_DIR)/midi_message_queue.cpp \
              $(SRC_DIR)/midi_event_handler.cpp

EXAMPLE_SOURCES = $(EXAMPLES_DIR)/led_patterns.cpp \
                  $(EXAMPLES_DIR)/midi_monitor.cpp

# Object files
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
GUI_OBJECTS = $(GUI_SOURCES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
EXAMPLE_OBJECTS = $(EXAMPLE_SOURCES:$(EXAMPLES_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
CPPFLAGS = -I$(SRC_DIR)

# Haiku-specific flags and libraries
HAIKU_CXXFLAGS = -D_HAIKU_BUILD
HAIKU_LIBS = -lbe -lmidi2 -lroot -ldevice

# Debug/Release configurations
DEBUG_FLAGS = -DDEBUG -g -O0
RELEASE_FLAGS = -DNDEBUG -O3 -s

# USB Raw access flags (Haiku-specific)
USB_RAW_FLAGS = -DUSE_USB_RAW

# Default target
.PHONY: all
all: debug gui

# Create object directory
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Debug build
.PHONY: debug
debug: CXXFLAGS += $(DEBUG_FLAGS) $(USB_RAW_FLAGS)
debug: $(OBJ_DIR) $(APP_NAME)_debug

# Release build
.PHONY: release
release: CXXFLAGS += $(RELEASE_FLAGS) $(USB_RAW_FLAGS)
release: $(OBJ_DIR) $(APP_NAME)

# Main application targets
$(APP_NAME): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(HAIKU_CXXFLAGS) -o $@ $^ $(HAIKU_LIBS)
	@echo "Built release version: $(APP_NAME)"

$(APP_NAME)_debug: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(HAIKU_CXXFLAGS) -o $@ $^ $(HAIKU_LIBS)
	@echo "Built debug version: $(APP_NAME)_debug"

# Object file compilation
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(HAIKU_CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(EXAMPLES_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(HAIKU_CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# GUI Application
GUI_APP_NAME = apc_mini_gui
.PHONY: gui
gui: $(GUI_APP_NAME)

$(GUI_APP_NAME): $(GUI_OBJECTS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(HAIKU_CXXFLAGS) -o $@ $^ $(HAIKU_LIBS)
	@echo "Built GUI application: $(GUI_APP_NAME)"

$(GUI_APP_NAME)_debug: $(GUI_OBJECTS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) $(HAIKU_CXXFLAGS) -o $@ $^ $(HAIKU_LIBS)
	@echo "Built GUI application (debug): $(GUI_APP_NAME)_debug"

# Example utilities
.PHONY: examples
examples: led_patterns midi_monitor

led_patterns: $(OBJ_DIR)/led_patterns.o $(OBJ_DIR)/usb_raw_midi.o
	$(CXX) $(CXXFLAGS) $(HAIKU_CXXFLAGS) -o $@ $^ $(HAIKU_LIBS)
	@echo "Built example: led_patterns"

midi_monitor: $(OBJ_DIR)/midi_monitor.o $(OBJ_DIR)/usb_raw_midi.o
	$(CXX) $(CXXFLAGS) $(HAIKU_CXXFLAGS) -o $@ $^ $(HAIKU_LIBS)
	@echo "Built example: midi_monitor"

# Benchmark tool
BENCHMARK_NAME = latency_benchmark
.PHONY: benchmark
benchmark: $(BENCHMARK_NAME)

$(BENCHMARK_NAME): $(OBJ_DIR)/latency_benchmark.o $(OBJ_DIR)/usb_haiku_midi.o
	$(CXX) $(CXXFLAGS) $(HAIKU_CXXFLAGS) -o $@ $^ $(HAIKU_LIBS)
	@echo "Built benchmark tool: $(BENCHMARK_NAME)"

# Testing targets
.PHONY: test
test: debug
	@echo "Running basic functionality test..."
	./$(APP_NAME)_debug --test-mode simulation

.PHONY: test-stress
test-stress: debug
	@echo "Running stress test..."
	./$(APP_NAME)_debug --test-mode stress

# Installation
INSTALL_DIR = /boot/home/Desktop/APC_Mini_Test
.PHONY: install
install: release
	mkdir -p $(INSTALL_DIR)
	cp $(APP_NAME) $(INSTALL_DIR)/
	cp -r ../docs $(INSTALL_DIR)/
	@echo "Installed to $(INSTALL_DIR)"

# Package creation (Haiku package format)
PACKAGE_NAME = apc_mini_test-$(APP_VERSION)-x86_64.hpkg
.PHONY: package
package: release examples
	@echo "Creating Haiku package..."
	mkdir -p package_tmp/apps/apc_mini_test
	mkdir -p package_tmp/documentation/apc_mini_test
	mkdir -p package_tmp/bin

	# Copy binaries
	cp $(APP_NAME) package_tmp/apps/apc_mini_test/
	cp led_patterns midi_monitor package_tmp/apps/apc_mini_test/

	# Copy documentation
	cp ../docs/* package_tmp/documentation/apc_mini_test/

	# Create package info
	echo 'name apc_mini_test' > package_tmp/.PackageInfo
	echo 'version $(APP_VERSION)' >> package_tmp/.PackageInfo
	echo 'architecture x86_64' >> package_tmp/.PackageInfo
	echo 'summary "APC Mini MIDI Controller Test Application"' >> package_tmp/.PackageInfo
	echo 'description "Comprehensive test suite for Akai APC Mini on Haiku OS"' >> package_tmp/.PackageInfo
	echo 'packager "Haiku Community"' >> package_tmp/.PackageInfo
	echo 'vendor "Open Source"' >> package_tmp/.PackageInfo
	echo 'licenses MIT' >> package_tmp/.PackageInfo
	echo 'copyrights "2024 APC Mini Test Project"' >> package_tmp/.PackageInfo
	echo 'requires {' >> package_tmp/.PackageInfo
	echo '    haiku >= r1~beta5' >> package_tmp/.PackageInfo
	echo '}' >> package_tmp/.PackageInfo

	# Create the package (requires Haiku package tools)
	package create -b package_tmp $(PACKAGE_NAME)
	rm -rf package_tmp
	@echo "Created package: $(PACKAGE_NAME)"

# Cleaning
.PHONY: clean
clean:
	rm -rf $(OBJ_DIR)
	rm -f $(APP_NAME) $(APP_NAME)_debug
	rm -f led_patterns midi_monitor
	rm -f *.hpkg
	rm -rf package_tmp
	@echo "Cleaned build artifacts"

.PHONY: distclean
distclean: clean
	rm -f ../docs/*.html
	@echo "Cleaned all generated files"

# Development helpers
.PHONY: format
format:
	@echo "Formatting source code..."
	find $(SRC_DIR) $(EXAMPLES_DIR) -name "*.cpp" -o -name "*.h" | xargs clang-format -i

.PHONY: lint
lint:
	@echo "Running static analysis..."
	cppcheck --enable=all --std=c++17 $(SRC_DIR)/ $(EXAMPLES_DIR)/

# USB device detection helper
.PHONY: detect-usb
detect-usb:
	@echo "Scanning for USB devices..."
	@if [ -d /dev/bus/usb ]; then \
		ls -la /dev/bus/usb/; \
	else \
		echo "USB raw access not available. Check Haiku USB stack."; \
	fi

# MIDI device detection helper
.PHONY: detect-midi
detect-midi:
	@echo "Scanning for MIDI devices..."
	@if command -v midiscan >/dev/null 2>&1; then \
		midiscan; \
	else \
		echo "MIDI scan tool not available. Check Haiku MIDI stack."; \
	fi

# Development server for file transfer to Haiku VM
.PHONY: serve
serve:
	@echo "Starting development server on port 8080..."
	@echo "Use this to transfer files to Haiku VM via wget"
	@cd .. && python3 -m http.server 8080

# Help target
.PHONY: help
help:
	@echo "APC Mini Test Application - Build System"
	@echo "========================================"
	@echo ""
	@echo "Main targets:"
	@echo "  all         - Build debug version (default)"
	@echo "  debug       - Build debug version with full symbols"
	@echo "  release     - Build optimized release version"
	@echo "  examples    - Build example utilities"
	@echo ""
	@echo "Testing:"
	@echo "  test        - Run basic functionality tests"
	@echo "  test-stress - Run stress tests"
	@echo ""
	@echo "Installation:"
	@echo "  install     - Install to $(INSTALL_DIR)"
	@echo "  package     - Create Haiku package (.hpkg)"
	@echo ""
	@echo "Utilities:"
	@echo "  detect-usb  - Scan for USB devices"
	@echo "  detect-midi - Scan for MIDI devices"
	@echo "  format      - Format source code"
	@echo "  lint        - Run static analysis"
	@echo "  serve       - Start development HTTP server"
	@echo ""
	@echo "Cleaning:"
	@echo "  clean       - Remove build artifacts"
	@echo "  distclean   - Remove all generated files"
	@echo ""
	@echo "Requirements:"
	@echo "  - Haiku R1/beta5 or later"
	@echo "  - GCC with C++17 support"
	@echo "  - Be/MIDI development libraries"
	@echo "  - USB raw access permissions"

# Dependencies
-include $(OBJECTS:.o=.d)

# Automatic dependency generation
$(OBJ_DIR)/%.d: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) -MM -MT '$(OBJ_DIR)/$*.o $@' $< > $@

$(OBJ_DIR)/%.d: $(EXAMPLES_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) -MM -MT '$(OBJ_DIR)/$*.o $@' $< > $@