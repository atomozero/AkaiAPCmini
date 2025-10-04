#!/bin/bash

# Phase 1 Validation Script for APC Mini Refactoring
# Validates that all new components integrate correctly

echo "APC Mini Phase 1 Refactoring - Validation Script"
echo "==============================================="
echo

# Color definitions for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored status
print_status() {
    local status=$1
    local message=$2
    case $status in
        "OK")
            echo -e "[${GREEN}✓${NC}] $message"
            ;;
        "WARN")
            echo -e "[${YELLOW}⚠${NC}] $message"
            ;;
        "ERROR")
            echo -e "[${RED}✗${NC}] $message"
            ;;
        "INFO")
            echo -e "[${BLUE}ℹ${NC}] $message"
            ;;
    esac
}

# Validation counters
ERRORS=0
WARNINGS=0

# Check if we're in the right directory
if [ ! -f "build/Makefile" ]; then
    print_status "ERROR" "Must be run from the apc_mini_project directory"
    exit 1
fi

print_status "INFO" "Starting Phase 1 validation checks..."
echo

# 1. Check source files exist
echo "1. Checking source file structure..."
REQUIRED_FILES=(
    "src/midi_message_queue.h"
    "src/midi_message_queue.cpp"
    "src/midi_event_handler.h"
    "src/midi_event_handler.cpp"
    "src/apc_mini_gui.h"
    "src/apc_mini_gui_main.cpp"
    "src/apc_mini_gui_app.cpp"
)

for file in "${REQUIRED_FILES[@]}"; do
    if [ -f "$file" ]; then
        print_status "OK" "Found $file"
    else
        print_status "ERROR" "Missing $file"
        ((ERRORS++))
    fi
done
echo

# 2. Check critical code patterns
echo "2. Checking critical code patterns..."

# Check for lock-free queue implementation
if grep -q "std::atomic" src/midi_message_queue.h; then
    print_status "OK" "Atomic operations found in MIDI queue"
else
    print_status "ERROR" "Missing atomic operations in MIDI queue"
    ((ERRORS++))
fi

# Check for BLayoutBuilder usage
if grep -q "BLayoutBuilder" src/apc_mini_gui_app.cpp; then
    print_status "OK" "BLayoutBuilder integration found"
else
    print_status "ERROR" "Missing BLayoutBuilder integration"
    ((ERRORS++))
fi

# Check for MIDI event handler integration
if grep -q "RegisterMIDICallbacks" src/apc_mini_gui_main.cpp; then
    print_status "OK" "MIDI callback registration found"
else
    print_status "ERROR" "Missing MIDI callback registration"
    ((ERRORS++))
fi

# Check for real-time safety
if grep -q "MIDI_PRIORITY_" src/midi_event_handler.h; then
    print_status "OK" "Real-time priority system found"
else
    print_status "ERROR" "Missing real-time priority system"
    ((ERRORS++))
fi
echo

# 3. Check Makefile integration
echo "3. Checking Makefile integration..."

if grep -q "midi_message_queue.cpp" build/Makefile; then
    print_status "OK" "MIDI queue added to Makefile"
else
    print_status "ERROR" "MIDI queue not in Makefile"
    ((ERRORS++))
fi

if grep -q "midi_event_handler.cpp" build/Makefile; then
    print_status "OK" "MIDI handler added to Makefile"
else
    print_status "ERROR" "MIDI handler not in Makefile"
    ((ERRORS++))
fi
echo

# 4. Check for common anti-patterns
echo "4. Checking for anti-patterns..."

# Check for hardcoded positioning (should be reduced)
HARDCODED_COUNT=$(grep -c "BRect.*[0-9].*[0-9]" src/apc_mini_gui_app.cpp || true)
if [ "$HARDCODED_COUNT" -lt 5 ]; then
    print_status "OK" "Reduced hardcoded positioning (${HARDCODED_COUNT} instances)"
else
    print_status "WARN" "Still many hardcoded positions (${HARDCODED_COUNT} instances)"
    ((WARNINGS++))
fi

# Check for direct GUI manipulation in callbacks
if grep -q "Window()->PostMessage" src/midi_event_handler.cpp; then
    print_status "WARN" "Direct window access in MIDI handler"
    ((WARNINGS++))
else
    print_status "OK" "No direct GUI manipulation in MIDI handler"
fi
echo

# 5. Architecture validation
echo "5. Validating architecture improvements..."

# Check for forward declarations
if grep -q "class MIDIMessageQueue;" src/apc_mini_gui.h; then
    print_status "OK" "Forward declarations found"
else
    print_status "WARN" "Missing forward declarations"
    ((WARNINGS++))
fi

# Check for proper member initialization
if grep -q "midi_queue(nullptr)" src/apc_mini_gui_main.cpp; then
    print_status "OK" "Proper member initialization"
else
    print_status "ERROR" "Missing proper initialization"
    ((ERRORS++))
fi

# Check for cleanup in destructor
if grep -q "delete midi_queue" src/apc_mini_gui_main.cpp; then
    print_status "OK" "Proper cleanup in destructor"
else
    print_status "ERROR" "Missing cleanup in destructor"
    ((ERRORS++))
fi
echo

# 6. Summary
echo "6. Validation Summary"
echo "===================="

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    print_status "OK" "All checks passed! Phase 1 ready for testing."
elif [ $ERRORS -eq 0 ]; then
    print_status "WARN" "Phase 1 ready with ${WARNINGS} warnings."
else
    print_status "ERROR" "Phase 1 has ${ERRORS} errors and ${WARNINGS} warnings."
    echo
    echo "Please fix the errors before proceeding to hardware testing."
    exit 1
fi

echo
print_status "INFO" "Next steps:"
echo "   1. Compile with: cd build && make clean && make gui"
echo "   2. Test with hardware: ./apc_mini_gui"
echo "   3. Check MIDI latency and performance"
echo "   4. Verify layout responsiveness"
echo
print_status "INFO" "Phase 1 refactoring validation complete!"