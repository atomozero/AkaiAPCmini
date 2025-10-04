# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an **APC Mini Test Application** for **Haiku OS** - a comprehensive MIDI controller diagnostic tool that provides direct USB Raw access to the Akai APC Mini controller. The project bypasses Haiku's incomplete USB MIDI stack while maintaining fallback compatibility with standard MIDI APIs.

## Development Commands

### Building
- **Debug build**: `cd build && make debug`
- **Release build**: `cd build && make release`
- **Build examples**: `cd build && make examples`
- **Clean build**: `cd build && make clean`

### Testing
- **Run tests**: `cd build && make test`
- **Stress testing**: `cd build && make test-stress`
- **Full test suite**: `./scripts/test_runner.sh`
- **Specific test suite**: `./scripts/test_runner.sh [unit|integration|simulation|stress|latency]`

### Hardware Detection
- **USB detection**: `cd build && make detect-usb`
- **MIDI detection**: `cd build && make detect-midi`

### Deployment
- **Deploy to Haiku VM**: `./scripts/deploy_to_haiku.sh serve`
- **Create package**: `cd build && make package`
- **Install locally**: `cd build && make install`

### Code Quality
- **Format code**: `cd build && make format`
- **Static analysis**: `cd build && make lint`

## Architecture

### Core Components
- **usb_raw_midi.cpp/h**: USB Raw access implementation for direct hardware communication
- **apc_mini_test.cpp**: Main application with Haiku MIDI integration and fallback
- **apc_mini_defs.h**: Hardware constants, MIDI mappings, and data structures

### Hardware Protocol
- **APC Mini Controller**: Akai USB MIDI device (VID: 0x09E8, PID: 0x0028)
- **Pad Matrix**: 8x8 grid (64 pads), MIDI notes 0x00-0x3F
- **Faders**: 8 track faders (CC 48-55) + 1 master fader (CC 56)
- **Buttons**: Track buttons (notes 100-107), Scene buttons (notes 112-119), Shift (note 122)
- **LED Colors**: Off(0x00), Green(0x01), Red(0x03), Yellow(0x05), with blink variants

### Dual-Access Design
The application uses a **primary USB Raw + fallback MIDI** architecture:
1. **USB Raw Access**: Direct USB communication bypassing MIDI stack (preferred)
2. **Haiku MIDI API**: Standard MIDI interface fallback when USB Raw fails
3. **Graceful Degradation**: Automatic fallback with feature preservation

### Threading Model
- **Main Thread**: User interface and application logic
- **USB Reader Thread**: Continuous USB message polling (when using USB Raw)
- **MIDI Thread**: Haiku MIDI message handling (when using MIDI fallback)

## Key File Locations

### Source Code
- **src/**: Core implementation files
- **examples/**: Example utilities (led_patterns, midi_monitor)
- **build/**: Makefile and build configuration

### Documentation
- **docs/README.md**: Comprehensive user documentation
- **docs/INSTALL.md**: Installation instructions
- **docs/TROUBLESHOOTING.md**: Common issues and solutions
- **FADER_MAPPING_MIGRATION.md**: Critical fix documentation for fader CC mapping

### Scripts
- **scripts/test_runner.sh**: Automated testing framework
- **scripts/deploy_to_haiku.sh**: Deployment automation for Haiku VM
- **scripts/setup_qemu.sh**: QEMU environment setup

## Development Guidelines

### Target Environment
- **OS**: Haiku R1/beta5 or later
- **Architecture**: x86_64 (primary), x86_gcc2 (legacy)
- **Compiler**: GCC with C++17 support
- **Libraries**: Be API, MIDI API, USB Raw access

### Critical Implementation Notes
1. **Fader Mapping**: Track faders use CC 48-55 (8 consecutive), Master fader uses CC 56 (separate)
2. **Array Sizing**: `track_fader_values[8]` for track faders, `master_fader_value` for master
3. **USB Permissions**: Requires USB raw device access permissions on Haiku
4. **Error Handling**: Always provide graceful fallback from USB Raw to MIDI API

### Testing Strategy
- **Hardware Testing**: Requires physical APC Mini for full validation
- **Simulation Mode**: Hardware-independent testing for development
- **Stress Testing**: High-throughput message testing (>1000 msg/sec)
- **Latency Testing**: Round-trip timing measurement for performance validation

### Build Targets
- **apc_mini_test_debug**: Debug version with full symbols
- **apc_mini_test**: Optimized release version
- **led_patterns**: LED demonstration utility
- **midi_monitor**: MIDI message monitoring tool

## Common Development Tasks

### Adding New MIDI Features
1. Update constants in `apc_mini_defs.h`
2. Implement USB Raw handling in `usb_raw_midi.cpp`
3. Add Haiku MIDI fallback in `apc_mini_test.cpp`
4. Update test cases in relevant test files

### Hardware Protocol Changes
1. Verify changes against official APC Mini documentation
2. Update constants and detection macros in `apc_mini_defs.h`
3. Test with both USB Raw and MIDI fallback modes
4. Update validation tests in `fader_mapping_test.cpp`

### Performance Optimization
1. Profile using built-in statistics gathering
2. Test with stress test mode (`make test-stress`)
3. Measure latency impact with latency test mode
4. Validate on real hardware before deployment