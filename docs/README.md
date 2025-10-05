# APC Mini Test Application for Haiku OS

A comprehensive test suite and diagnostic tool for the **Akai APC Mini MIDI controller** on **Haiku OS**. This application provides direct USB Raw access to bypass Haiku's incomplete USB MIDI stack, with graceful fallback to standard MIDI APIs.

## Features

### Core Functionality
- **64-Pad Matrix Testing**: Full 8x8 grid pressure-sensitive pad support
- **9 Fader Control**: 8 channel faders + master fader with CC feedback
- **LED Control**: Complete color support (Green/Red/Yellow + Blink modes)
- **Button Testing**: Track buttons (8), Scene buttons (8), and Shift modifier
- **Performance Metrics**: Real-time latency monitoring and message counting

### Advanced Features
- **USB Raw Access**: Direct USB communication bypassing MIDI stack
- **Dual-Mode Operation**: USB Raw primary, Haiku MIDI fallback
- **Interactive Testing**: Real-time command interface
- **Simulation Mode**: Test without physical hardware
- **Stress Testing**: High-throughput message testing (>1000 msg/sec)
- **Latency Analysis**: Round-trip timing measurement

## Requirements

### System Requirements
- **Haiku OS**: R1/beta5 or later
- **Architecture**: x86_64 (primary), x86_gcc2 (legacy support)
- **Development Tools**: GCC with C++17 support
- **Libraries**: Be API, MIDI API, USB Raw access

### Hardware Requirements
- **Akai APC Mini**: USB MIDI controller (VID: 0x09E8, PID: 0x0028)
- **USB Port**: Direct USB connection required
- **Permissions**: USB raw device access permissions

## Quick Start

### 1. Clone and Build
```bash
# Clone the repository
cd /boot/home/Desktop
git clone https://github.com/atomozero/AkaiAPCmini.git
cd AkaiAPCmini/build

# Build debug version
make debug

# Or build release version
make release

# Optional: Build performance benchmarks (separate project)
cd ../benchmarks
make
```

### 2. Connect Hardware
```bash
# Connect APC Mini via USB
# Check device detection
make detect-usb
make detect-midi
```

### 3. Run Application
```bash
# Interactive mode (recommended for first use)
./apc_mini_test_debug

# Simulation mode (no hardware required)
./apc_mini_test_debug --simulation

# Stress test mode
./apc_mini_test_debug --stress
```

## Usage Guide

### Interactive Commands
Once the application is running, use these single-key commands:

| Key | Function | Description |
|-----|----------|-------------|
| `h` | Help | Show command help |
| `s` | Statistics | Display performance stats |
| `t` | Test Pads | Test all 64 pads sequentially |
| `f` | Test Faders | Monitor fader movements |
| `b` | Test Buttons | Monitor button presses |
| `c` | Test Colors | Cycle through LED colors |
| `r` | Reset | Reset device and clear stats |
| `v` | View State | Show current device state |
| `p` | Pad Matrix | Show pad matrix visualization |
| `l` | Latency Test | Measure round-trip latency |
| `x` | Stress Test | High-throughput message test |
| `m` | Simulation | Run simulation mode |
| `q` | Quit | Exit application |

### Test Modes

#### Interactive Mode (Default)
Real-time interaction with connected APC Mini:
```bash
./apc_mini_test_debug
```

#### Simulation Mode
Test functionality without hardware:
```bash
./apc_mini_test_debug --simulation
```

#### Stress Test Mode
High-performance message testing:
```bash
./apc_mini_test_debug --stress
```

#### Latency Test Mode
Measure response times:
```bash
./apc_mini_test_debug --latency
```

## Architecture

### Dual-Access Design
```
┌─────────────────┐    ┌──────────────────┐
│  Application    │    │  APC Mini Test   │
│     Layer       │    │   Application    │
└─────────────────┘    └──────────────────┘
         │                       │
         ▼                       ▼
┌─────────────────┐    ┌──────────────────┐
│   USB Raw       │    │  Haiku MIDI API │
│    Access       │    │   (Fallback)     │
└─────────────────┘    └──────────────────┘
         │                       │
         ▼                       ▼
┌─────────────────┐    ┌──────────────────┐
│ /dev/bus/usb/   │    │   MIDI Roster    │
│     raw         │    │   BMidiConsumer  │
└─────────────────┘    └──────────────────┘
         │                       │
         └───────────┬───────────┘
                     ▼
            ┌──────────────────┐
            │   APC Mini       │
            │  Hardware        │
            └──────────────────┘
```

### Module Structure
- **apc_mini_test.cpp**: Main application with Haiku MIDI integration
- **usb_raw_midi.cpp**: USB Raw access implementation
- **apc_mini_defs.h**: Constants, structures, and MIDI mappings

## MIDI Mapping

### Pad Matrix (8x8 = 64 pads)
```
Coordinates: (x,y) where x=0-7, y=0-7
MIDI Notes: 0x00-0x3F (0-63)
Formula: note = y * 8 + x
```

### Controls
- **Faders**: CC 0x30-0x38 (48-56), 9 faders total
- **Track Buttons**: Notes 0x64-0x6B (100-107)
- **Scene Buttons**: Notes 0x70-0x77 (112-119)
- **Shift Button**: Note 0x7A (122)

### LED Colors
- `0x00`: Off
- `0x01`: Green
- `0x02`: Green Blink
- `0x03`: Red
- `0x04`: Red Blink
- `0x05`: Yellow
- `0x06`: Yellow Blink

## Performance Metrics

### Expected Performance
- **Latency**: < 10ms round-trip
- **Throughput**: > 1000 messages/second
- **USB Packet Size**: 4 bytes (USB MIDI format)
- **MIDI Message Size**: 3 bytes (standard MIDI)

### Monitoring
The application provides real-time statistics:
- Messages sent/received
- Pad/fader/button interaction counts
- Latency measurements (min/max/average)
- Error counts and recovery

## Troubleshooting

### Common Issues

#### USB Raw Access Failed
```
Error: APC_ERROR_USB_OPEN_FAILED
Solution: Check USB permissions and device connection
```

#### MIDI Initialization Failed
```
Error: APC_ERROR_MIDI_INIT_FAILED
Solution: Verify MIDI services are running
```

#### Device Not Found
```
Error: APC_ERROR_DEVICE_NOT_FOUND
Solution: Check USB connection and device power
```

### Debug Commands
```bash
# Check USB devices
make detect-usb
ls -la /dev/bus/usb/

# Check MIDI devices
make detect-midi

# Verify permissions
ls -la /dev/bus/usb/raw*

# Run with debug output
./apc_mini_test_debug --verbose
```

### Hardware Verification
1. **Power LED**: APC Mini should show power LED
2. **USB Connection**: Device should appear in USB scan
3. **MIDI Response**: Test with external MIDI software first

## Development

### Building
```bash
# Debug build with symbols
make debug

# Release build optimized
make release

# Build examples
make examples

# Clean build artifacts
make clean

# Build performance benchmarks (in benchmarks/ directory)
cd ../benchmarks
make
```

### IDE Integration
The project includes a **Genio IDE** project file:
```bash
# Open in Genio
genio build/apc_mini.gproject
```

### Code Style
- **Standard**: C++17
- **Style**: Allman braces, 4-space indents
- **Formatting**: Use `make format`
- **Analysis**: Use `make lint`

## Examples

The project includes example utilities:

### LED Patterns Demo
```bash
make examples
./led_patterns
```

### MIDI Monitor
```bash
./midi_monitor
```

## Installation

### System Installation
```bash
make install
# Installs to /boot/home/Desktop/APC_Mini_Test/
```

### Package Creation
```bash
make package
# Creates apc_mini_test-1.0.0-x86_64.hpkg
```

## License

MIT License - See project files for full license text.

## Contributing

1. **Code Style**: Follow existing conventions
2. **Testing**: Test on real hardware when possible
3. **Documentation**: Update docs for new features
4. **Compatibility**: Maintain Haiku R1/beta5+ compatibility

## Performance Benchmarks

The project includes a separate benchmark suite in `benchmarks/` for testing MIDI subsystem performance:

- **Virtual MIDI Benchmark**: Tests pure MidiKit routing overhead (no hardware required)
- **MidiKit Driver Test**: Tests hardware driver blocking behavior (requires APC Mini)

See [benchmarks/README.md](../benchmarks/README.md) for details and usage.

## Support

### Resources
- **Haiku Documentation**: https://www.haiku-os.org/docs/
- **APC Mini Manual**: Akai Professional documentation
- **MIDI Specification**: https://midi.org/specifications

### Reporting Issues
1. Include Haiku version and revision
2. Provide hardware information (APC Mini model/firmware)
3. Include debug output and error messages
4. Test with both USB Raw and MIDI fallback modes

---

**APC Mini Test Application v1.0.0**
*Comprehensive MIDI controller testing for Haiku OS*