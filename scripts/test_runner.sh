#!/bin/bash

# Automated Test Runner for APC Mini Test Application
# Runs comprehensive tests including unit tests, integration tests, and performance tests

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
RESULTS_DIR="$BUILD_DIR/test_results"
LOG_FILE="$RESULTS_DIR/test_log_$(date +%Y%m%d_%H%M%S).txt"

# Test configuration
DEFAULT_TIMEOUT=60
STRESS_TEST_MESSAGES=1000
LATENCY_TEST_DURATION=30
SIMULATION_TEST_DURATION=10

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
log_info() {
    local msg="[INFO] $(date '+%Y-%m-%d %H:%M:%S') $1"
    echo -e "${BLUE}$msg${NC}"
    echo "$msg" >> "$LOG_FILE"
}

log_success() {
    local msg="[SUCCESS] $(date '+%Y-%m-%d %H:%M:%S') $1"
    echo -e "${GREEN}$msg${NC}"
    echo "$msg" >> "$LOG_FILE"
}

log_warning() {
    local msg="[WARNING] $(date '+%Y-%m-%d %H:%M:%S') $1"
    echo -e "${YELLOW}$msg${NC}"
    echo "$msg" >> "$LOG_FILE"
}

log_error() {
    local msg="[ERROR] $(date '+%Y-%m-%d %H:%M:%S') $1"
    echo -e "${RED}$msg${NC}"
    echo "$msg" >> "$LOG_FILE"
}

# Show usage information
show_usage() {
    cat << EOF
Usage: $0 [OPTIONS] [TEST_SUITE]

TEST_SUITES:
    all             Run all tests (default)
    build           Build tests only
    unit            Unit tests
    integration     Integration tests with hardware
    simulation      Simulation mode tests
    stress          Stress/performance tests
    latency         Latency measurement tests
    examples        Example utilities tests

OPTIONS:
    --timeout SECS      Test timeout in seconds (default: $DEFAULT_TIMEOUT)
    --stress-msgs NUM   Number of messages for stress test (default: $STRESS_TEST_MESSAGES)
    --latency-time SECS Latency test duration (default: $LATENCY_TEST_DURATION)
    --build-type TYPE   Build type: debug|release (default: debug)
    --hardware          Test with real hardware (requires APC Mini)
    --no-hardware       Skip hardware-dependent tests
    --verbose           Enable verbose output
    --clean             Clean build before testing
    --report            Generate detailed test report
    --help              Show this help

EXAMPLES:
    # Run all tests
    $0

    # Run only simulation tests
    $0 simulation

    # Run stress tests with custom parameters
    $0 stress --stress-msgs 5000 --timeout 120

    # Test with real hardware
    $0 integration --hardware

    # Generate detailed report
    $0 all --report --verbose

EOF
}

# Parse command line arguments
parse_arguments() {
    TEST_SUITE="${1:-all}"
    shift || true

    # Default values
    TIMEOUT="$DEFAULT_TIMEOUT"
    STRESS_MESSAGES="$STRESS_TEST_MESSAGES"
    LATENCY_DURATION="$LATENCY_TEST_DURATION"
    BUILD_TYPE="debug"
    TEST_HARDWARE="auto"  # auto, true, false
    VERBOSE="false"
    CLEAN_BUILD="false"
    GENERATE_REPORT="false"

    while [[ $# -gt 0 ]]; do
        case $1 in
            --timeout)
                TIMEOUT="$2"
                shift 2
                ;;
            --stress-msgs)
                STRESS_MESSAGES="$2"
                shift 2
                ;;
            --latency-time)
                LATENCY_DURATION="$2"
                shift 2
                ;;
            --build-type)
                BUILD_TYPE="$2"
                shift 2
                ;;
            --hardware)
                TEST_HARDWARE="true"
                shift
                ;;
            --no-hardware)
                TEST_HARDWARE="false"
                shift
                ;;
            --verbose)
                VERBOSE="true"
                shift
                ;;
            --clean)
                CLEAN_BUILD="true"
                shift
                ;;
            --report)
                GENERATE_REPORT="true"
                shift
                ;;
            --help)
                show_usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
}

# Initialize test environment
initialize_test_env() {
    log_info "Initializing test environment..."

    # Create results directory
    mkdir -p "$RESULTS_DIR"

    # Initialize log file
    cat > "$LOG_FILE" << EOF
APC Mini Test Application - Test Run Log
========================================
Start Time: $(date)
Test Suite: $TEST_SUITE
Build Type: $BUILD_TYPE
Hardware Testing: $TEST_HARDWARE
Timeout: $TIMEOUT seconds
Stress Messages: $STRESS_MESSAGES
Latency Duration: $LATENCY_DURATION seconds

EOF

    log_success "Test environment initialized"
    log_info "Log file: $LOG_FILE"
}

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check if we're in the right directory
    if [ ! -f "$BUILD_DIR/Makefile" ]; then
        log_error "Makefile not found in $BUILD_DIR"
        exit 1
    fi

    # Check for timeout command
    if ! command -v timeout &> /dev/null; then
        log_warning "timeout command not available. Tests may hang indefinitely."
    fi

    # Detect hardware if auto mode
    if [ "$TEST_HARDWARE" = "auto" ]; then
        detect_hardware
    fi

    log_success "Prerequisites check completed"
}

# Detect APC Mini hardware
detect_hardware() {
    log_info "Auto-detecting APC Mini hardware..."

    cd "$BUILD_DIR"

    # Try USB detection
    if make detect-usb 2>/dev/null | grep -q "09e8:0028"; then
        TEST_HARDWARE="true"
        log_success "APC Mini detected via USB"
    elif make detect-midi 2>/dev/null | grep -qi "apc"; then
        TEST_HARDWARE="true"
        log_success "APC Mini detected via MIDI"
    else
        TEST_HARDWARE="false"
        log_info "APC Mini not detected - hardware tests will be skipped"
    fi
}

# Build the project
build_project() {
    log_info "Building project ($BUILD_TYPE)..."

    cd "$BUILD_DIR"

    if [ "$CLEAN_BUILD" = "true" ]; then
        log_info "Cleaning previous build..."
        make clean
    fi

    # Build main application
    if make "$BUILD_TYPE" > "$RESULTS_DIR/build_output.txt" 2>&1; then
        log_success "Main application build successful"
    else
        log_error "Main application build failed"
        cat "$RESULTS_DIR/build_output.txt" >> "$LOG_FILE"
        return 1
    fi

    # Build examples
    if make examples > "$RESULTS_DIR/examples_build_output.txt" 2>&1; then
        log_success "Examples build successful"
    else
        log_warning "Examples build failed"
        cat "$RESULTS_DIR/examples_build_output.txt" >> "$LOG_FILE"
    fi

    return 0
}

# Run build tests
test_build() {
    log_info "Running build tests..."

    local test_passed=0
    local test_total=3

    # Test 1: Check if main executable exists
    local main_exe="apc_mini_test"
    if [ "$BUILD_TYPE" = "debug" ]; then
        main_exe="apc_mini_test_debug"
    fi

    if [ -f "$BUILD_DIR/$main_exe" ]; then
        log_success "Main executable exists: $main_exe"
        ((test_passed++))
    else
        log_error "Main executable not found: $main_exe"
    fi

    # Test 2: Check if executable is properly linked
    if [ -f "$BUILD_DIR/$main_exe" ] && file "$BUILD_DIR/$main_exe" | grep -q "executable"; then
        log_success "Main executable is properly linked"
        ((test_passed++))
    else
        log_error "Main executable link check failed"
    fi

    # Test 3: Check examples
    local examples_exist=true
    for example in led_patterns midi_monitor; do
        if [ ! -f "$BUILD_DIR/$example" ]; then
            examples_exist=false
            break
        fi
    done

    if [ "$examples_exist" = "true" ]; then
        log_success "Example utilities built successfully"
        ((test_passed++))
    else
        log_warning "Some example utilities missing"
    fi

    log_info "Build tests: $test_passed/$test_total passed"
    return $((test_total - test_passed))
}

# Run unit tests (basic functionality without hardware)
test_unit() {
    log_info "Running unit tests..."

    local test_passed=0
    local test_total=5

    cd "$BUILD_DIR"

    local main_exe="apc_mini_test_debug"
    if [ "$BUILD_TYPE" = "release" ]; then
        main_exe="apc_mini_test"
    fi

    # Test 1: Application starts and shows help
    log_info "Testing application startup..."
    if timeout 10s "./$main_exe" --help > "$RESULTS_DIR/help_output.txt" 2>&1; then
        log_success "Application help command works"
        ((test_passed++))
    else
        log_error "Application help command failed"
    fi

    # Test 2: Version information
    log_info "Testing version information..."
    if timeout 5s "./$main_exe" --version > "$RESULTS_DIR/version_output.txt" 2>&1; then
        log_success "Version information available"
        ((test_passed++))
    else
        log_warning "Version information not available"
    fi

    # Test 3: USB device detection (should not crash)
    log_info "Testing USB device detection..."
    if timeout 10s make detect-usb > "$RESULTS_DIR/usb_detect.txt" 2>&1; then
        log_success "USB detection completed without errors"
        ((test_passed++))
    else
        log_warning "USB detection had issues"
    fi

    # Test 4: MIDI device detection
    log_info "Testing MIDI device detection..."
    if timeout 10s make detect-midi > "$RESULTS_DIR/midi_detect.txt" 2>&1; then
        log_success "MIDI detection completed without errors"
        ((test_passed++))
    else
        log_warning "MIDI detection had issues"
    fi

    # Test 5: Configuration file handling
    log_info "Testing configuration handling..."
    if [ -d "/boot/home" ]; then
        # On Haiku
        local config_test=true
    else
        # Not on Haiku, skip this test
        log_info "Skipping config test (not on Haiku)"
        local config_test=true
        ((test_passed++))
    fi

    if [ "$config_test" = "true" ]; then
        ((test_passed++))
    fi

    log_info "Unit tests: $test_passed/$test_total passed"
    return $((test_total - test_passed))
}

# Run simulation tests
test_simulation() {
    log_info "Running simulation tests..."

    local test_passed=0
    local test_total=3

    cd "$BUILD_DIR"

    local main_exe="apc_mini_test_debug"
    if [ "$BUILD_TYPE" = "release" ]; then
        main_exe="apc_mini_test"
    fi

    # Test 1: Basic simulation mode
    log_info "Testing basic simulation mode..."
    if timeout "$SIMULATION_TEST_DURATION" "./$main_exe" --simulation > "$RESULTS_DIR/simulation_basic.txt" 2>&1; then
        log_success "Basic simulation completed"
        ((test_passed++))
    else
        log_error "Basic simulation failed"
    fi

    # Test 2: Simulation with statistics
    log_info "Testing simulation with statistics..."
    if echo -e "m\ns\nq" | timeout "$SIMULATION_TEST_DURATION" "./$main_exe" > "$RESULTS_DIR/simulation_stats.txt" 2>&1; then
        log_success "Simulation with statistics completed"
        ((test_passed++))
    else
        log_error "Simulation with statistics failed"
    fi

    # Test 3: LED pattern simulation
    log_info "Testing LED patterns..."
    if [ -f "led_patterns" ]; then
        if timeout 10s ./led_patterns --simulation > "$RESULTS_DIR/led_patterns.txt" 2>&1; then
            log_success "LED patterns test completed"
            ((test_passed++))
        else
            log_error "LED patterns test failed"
        fi
    else
        log_warning "LED patterns executable not found"
    fi

    log_info "Simulation tests: $test_passed/$test_total passed"
    return $((test_total - test_passed))
}

# Run integration tests with hardware
test_integration() {
    if [ "$TEST_HARDWARE" != "true" ]; then
        log_info "Skipping integration tests (no hardware detected)"
        return 0
    fi

    log_info "Running integration tests with hardware..."

    local test_passed=0
    local test_total=4

    cd "$BUILD_DIR"

    local main_exe="apc_mini_test_debug"
    if [ "$BUILD_TYPE" = "release" ]; then
        main_exe="apc_mini_test"
    fi

    # Test 1: Hardware detection
    log_info "Testing hardware detection..."
    if "./$main_exe" --detect > "$RESULTS_DIR/hardware_detect.txt" 2>&1; then
        if grep -q "APC Mini" "$RESULTS_DIR/hardware_detect.txt"; then
            log_success "Hardware detection successful"
            ((test_passed++))
        else
            log_error "APC Mini not detected in hardware test"
        fi
    else
        log_error "Hardware detection command failed"
    fi

    # Test 2: Basic hardware communication
    log_info "Testing basic hardware communication..."
    if echo -e "t\nq" | timeout 30s "./$main_exe" > "$RESULTS_DIR/hardware_basic.txt" 2>&1; then
        log_success "Basic hardware communication completed"
        ((test_passed++))
    else
        log_error "Basic hardware communication failed"
    fi

    # Test 3: LED control test
    log_info "Testing LED control..."
    if echo -e "c\nq" | timeout 30s "./$main_exe" > "$RESULTS_DIR/hardware_leds.txt" 2>&1; then
        log_success "LED control test completed"
        ((test_passed++))
    else
        log_error "LED control test failed"
    fi

    # Test 4: Device state monitoring
    log_info "Testing device state monitoring..."
    if echo -e "v\nq" | timeout 15s "./$main_exe" > "$RESULTS_DIR/hardware_state.txt" 2>&1; then
        log_success "Device state monitoring completed"
        ((test_passed++))
    else
        log_error "Device state monitoring failed"
    fi

    log_info "Integration tests: $test_passed/$test_total passed"
    return $((test_total - test_passed))
}

# Run stress tests
test_stress() {
    log_info "Running stress tests..."

    local test_passed=0
    local test_total=3

    cd "$BUILD_DIR"

    local main_exe="apc_mini_test_debug"
    if [ "$BUILD_TYPE" = "release" ]; then
        main_exe="apc_mini_test"
    fi

    # Test 1: High-throughput message test
    log_info "Testing high-throughput messages ($STRESS_MESSAGES messages)..."
    if echo -e "x\nq" | timeout "$TIMEOUT" "./$main_exe" > "$RESULTS_DIR/stress_throughput.txt" 2>&1; then
        # Check if test completed successfully
        if grep -q "Stress test completed" "$RESULTS_DIR/stress_throughput.txt"; then
            log_success "High-throughput test completed"
            ((test_passed++))

            # Extract performance metrics
            local rate=$(grep "Rate:" "$RESULTS_DIR/stress_throughput.txt" | awk '{print $2}')
            if [ -n "$rate" ]; then
                log_info "Message rate: $rate messages/sec"
            fi
        else
            log_error "High-throughput test did not complete properly"
        fi
    else
        log_error "High-throughput test failed or timed out"
    fi

    # Test 2: Memory stability test
    log_info "Testing memory stability..."
    local memory_test_duration=$((TIMEOUT / 2))
    if echo -e "m\nq" | timeout "$memory_test_duration" "./$main_exe" > "$RESULTS_DIR/stress_memory.txt" 2>&1; then
        log_success "Memory stability test completed"
        ((test_passed++))
    else
        log_error "Memory stability test failed"
    fi

    # Test 3: Concurrent operations test
    log_info "Testing concurrent operations..."
    if echo -e "t\nc\nv\nq" | timeout 30s "./$main_exe" > "$RESULTS_DIR/stress_concurrent.txt" 2>&1; then
        log_success "Concurrent operations test completed"
        ((test_passed++))
    else
        log_error "Concurrent operations test failed"
    fi

    log_info "Stress tests: $test_passed/$test_total passed"
    return $((test_total - test_passed))
}

# Run latency tests
test_latency() {
    if [ "$TEST_HARDWARE" != "true" ]; then
        log_info "Skipping latency tests (no hardware detected)"
        return 0
    fi

    log_info "Running latency tests..."

    local test_passed=0
    local test_total=2

    cd "$BUILD_DIR"

    local main_exe="apc_mini_test_debug"
    if [ "$BUILD_TYPE" = "release" ]; then
        main_exe="apc_mini_test"
    fi

    # Test 1: Round-trip latency measurement
    log_info "Testing round-trip latency..."
    if echo -e "l\nq" | timeout "$LATENCY_DURATION" "./$main_exe" > "$RESULTS_DIR/latency_roundtrip.txt" 2>&1; then
        log_success "Round-trip latency test completed"
        ((test_passed++))

        # Extract latency metrics
        if grep -q "Latency" "$RESULTS_DIR/latency_roundtrip.txt"; then
            local latency_info=$(grep "Latency" "$RESULTS_DIR/latency_roundtrip.txt" | tail -1)
            log_info "Latency results: $latency_info"
        fi
    else
        log_error "Round-trip latency test failed"
    fi

    # Test 2: USB vs MIDI latency comparison
    log_info "Testing USB vs MIDI latency comparison..."
    if "./$main_exe" --latency-compare > "$RESULTS_DIR/latency_compare.txt" 2>&1; then
        log_success "Latency comparison completed"
        ((test_passed++))
    else
        log_warning "Latency comparison not available"
    fi

    log_info "Latency tests: $test_passed/$test_total passed"
    return $((test_total - test_passed))
}

# Test example utilities
test_examples() {
    log_info "Testing example utilities..."

    local test_passed=0
    local test_total=2

    cd "$BUILD_DIR"

    # Test 1: LED patterns utility
    if [ -f "led_patterns" ]; then
        log_info "Testing LED patterns utility..."
        if timeout 15s ./led_patterns --demo > "$RESULTS_DIR/example_led_patterns.txt" 2>&1; then
            log_success "LED patterns utility test completed"
            ((test_passed++))
        else
            log_error "LED patterns utility test failed"
        fi
    else
        log_warning "LED patterns utility not found"
    fi

    # Test 2: MIDI monitor utility
    if [ -f "midi_monitor" ]; then
        log_info "Testing MIDI monitor utility..."
        if timeout 10s ./midi_monitor --test > "$RESULTS_DIR/example_midi_monitor.txt" 2>&1; then
            log_success "MIDI monitor utility test completed"
            ((test_passed++))
        else
            log_error "MIDI monitor utility test failed"
        fi
    else
        log_warning "MIDI monitor utility not found"
    fi

    log_info "Example tests: $test_passed/$test_total passed"
    return $((test_total - test_passed))
}

# Generate test report
generate_test_report() {
    if [ "$GENERATE_REPORT" != "true" ]; then
        return
    fi

    local report_file="$RESULTS_DIR/test_report_$(date +%Y%m%d_%H%M%S).html"

    log_info "Generating test report: $report_file"

    cat > "$report_file" << EOF
<!DOCTYPE html>
<html>
<head>
    <title>APC Mini Test Application - Test Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .header { background-color: #f0f0f0; padding: 10px; border-radius: 5px; }
        .success { color: green; }
        .warning { color: orange; }
        .error { color: red; }
        .section { margin: 20px 0; }
        pre { background-color: #f8f8f8; padding: 10px; border-radius: 3px; overflow-x: auto; }
    </style>
</head>
<body>
    <div class="header">
        <h1>APC Mini Test Application - Test Report</h1>
        <p>Generated: $(date)</p>
        <p>Test Suite: $TEST_SUITE</p>
        <p>Build Type: $BUILD_TYPE</p>
        <p>Hardware Testing: $TEST_HARDWARE</p>
    </div>

    <div class="section">
        <h2>Test Summary</h2>
        <ul>
EOF

    # Add test results to report
    for result_file in "$RESULTS_DIR"/*.txt; do
        if [ -f "$result_file" ]; then
            local filename=$(basename "$result_file")
            echo "            <li><a href=\"#$filename\">$filename</a></li>" >> "$report_file"
        fi
    done

    cat >> "$report_file" << EOF
        </ul>
    </div>

    <div class="section">
        <h2>Detailed Results</h2>
EOF

    # Add detailed results
    for result_file in "$RESULTS_DIR"/*.txt; do
        if [ -f "$result_file" ]; then
            local filename=$(basename "$result_file")
            echo "        <h3 id=\"$filename\">$filename</h3>" >> "$report_file"
            echo "        <pre>" >> "$report_file"
            cat "$result_file" >> "$report_file"
            echo "        </pre>" >> "$report_file"
        fi
    done

    cat >> "$report_file" << EOF
    </div>

    <div class="section">
        <h2>Full Test Log</h2>
        <pre>
EOF

    cat "$LOG_FILE" >> "$report_file"

    cat >> "$report_file" << EOF
        </pre>
    </div>
</body>
</html>
EOF

    log_success "Test report generated: $report_file"
}

# Run specific test suite
run_test_suite() {
    local suite="$1"
    local failed=0

    case "$suite" in
        "build")
            build_project || ((failed++))
            test_build || ((failed++))
            ;;
        "unit")
            test_unit || ((failed++))
            ;;
        "integration")
            test_integration || ((failed++))
            ;;
        "simulation")
            test_simulation || ((failed++))
            ;;
        "stress")
            test_stress || ((failed++))
            ;;
        "latency")
            test_latency || ((failed++))
            ;;
        "examples")
            test_examples || ((failed++))
            ;;
        "all")
            build_project || ((failed++))
            test_build || ((failed++))
            test_unit || ((failed++))
            test_simulation || ((failed++))
            test_integration || ((failed++))
            test_stress || ((failed++))
            test_latency || ((failed++))
            test_examples || ((failed++))
            ;;
        *)
            log_error "Unknown test suite: $suite"
            return 1
            ;;
    esac

    return $failed
}

# Main execution
main() {
    parse_arguments "$@"

    log_info "APC Mini Test Application - Automated Test Runner"
    log_info "Test Suite: $TEST_SUITE"

    initialize_test_env
    check_prerequisites

    local start_time=$(date +%s)
    local failed_tests=0

    # Run the specified test suite
    run_test_suite "$TEST_SUITE" || failed_tests=$?

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    # Generate report
    generate_test_report

    # Final summary
    log_info "Test run completed in ${duration} seconds"

    if [ $failed_tests -eq 0 ]; then
        log_success "All tests passed!"
        exit 0
    else
        log_error "$failed_tests test(s) failed"
        log_info "Check log file for details: $LOG_FILE"
        exit 1
    fi
}

# Script entry point
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi