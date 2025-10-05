# Haiku MIDI Performance Benchmarks

Independent benchmark suite for testing Haiku OS MIDI subsystem performance.

## Purpose

This project provides isolated performance benchmarks to:

1. **Establish baseline metrics** for Haiku MidiKit performance
2. **Identify bottlenecks** in MIDI routing and driver layers
3. **Document bugs** in Haiku's USB MIDI implementation
4. **Compare approaches** (MidiKit vs USB Raw access)
5. **Provide evidence** for performance optimization decisions

## Project Structure

```
benchmarks/
├── Makefile                       # Independent build system
├── README.md                      # This file
├── RESULTS.md                     # Benchmark results and analysis
├── src/
│   ├── virtual_midi_benchmark.cpp # Pure MidiKit routing test
│   ├── midikit_driver_test.cpp    # Hardware driver test
│   └── apc_mini_defs.h            # Shared constants
├── results/                       # Timestamped test outputs
├── scripts/                       # Automation scripts
└── docs/                          # Additional documentation
```

## Benchmarks

### 1. Virtual MIDI Benchmark

**Purpose**: Measure pure MidiKit routing overhead without hardware

**What it tests**:
- Virtual producer → virtual consumer routing
- MidiKit internal latency
- Message throughput limits
- Batch operation performance

**Hardware required**: None (fully virtual)

**Run**:
```bash
make virtual
./virtual_midi_benchmark
```

**Typical results**:
- Latency: ~200-500 μs per message
- Throughput: ~2,000-5,000 msg/sec
- Batch 64 msgs: ~30-35 ms
- Reliability: 100% (0 lost messages)

**Key finding**: MidiKit has significant overhead even for virtual routing (~270 μs avg). This is due to the MIDI Kit 2 client-server architecture which requires Inter-Process Communication (IPC) for every MIDI message. USB/hardware adds additional latency on top of this baseline.

**Architecture Details**: MIDI Kit 2 uses a client-server model with a centralized `midi_server` process:
- **Client-Server Model**: Each application communicates with `midi_server` via IPC
- **Message Pipeline**: App → libmidi2 → midi_server → libmidi2 → Target App
- **IPC Overhead**: ~160μs per message (2 context switches + serialization)
- **Proxy Objects**: Each endpoint accessed through proxy (adds serialization cost)
- **Protected Memory**: Full address space isolation between applications
- **No Batching**: Each MIDI message sent individually (no bulk IPC optimization)

**Performance Breakdown (~270μs total)**:
- Serialization: ~50μs (encode MIDI message for IPC)
- IPC to server: ~80μs (context switch + message passing)
- Server routing: ~30μs (endpoint lookup + filtering)
- IPC to consumer: ~80μs (context switch + message passing)
- Deserialization: ~30μs (decode MIDI message)

**References**:
- [MIDI Kit 2 Design](https://www.freelists.org/post/openbeos-midi/Midi2-todo-List,1)
- [Client-Server Architecture](https://www.freelists.org/post/openbeos-midi/Midi2-todo-List,3)
- [OpenBeOS Newsletter #33](https://www.haiku-os.org/legacy-docs/openbeosnewsletter/nsl33.html)

---

### 2. MidiKit Driver Test

**Purpose**: Test if blocking occurs in Haiku's midi_usb driver

**What it tests**:
- BMidiPort direct device access
- BMidiRoster endpoint discovery
- Batch LED write performance
- Driver blocking detection
- Fallback mechanisms

**Hardware required**: APC Mini (or compatible USB MIDI device)

**Run**:
```bash
make driver
./midikit_driver_test
```

**Expected behavior**:
1. Attempts BMidiRoster connection (MIDI Kit 2)
2. Falls back to direct `/dev/midi/usb/` access (MIDI Kit 1)
3. Sends 10 batches of 64 LED commands
4. Measures timing to detect blocking

**Known issues**:
- **BMidiRoster empty**: Driver doesn't publish endpoints (Haiku bug)
- **Crashes on batch writes**: "Kill Thread" error during heavy traffic
- **Blocking detected**: Confirms driver has concurrency issues

---

## Building

### Requirements

- **OS**: Haiku R1/beta5 or later
- **Compiler**: GCC with C++17 support
- **Libraries**: libmidi, libmidi2, libbe

### Build All

```bash
cd benchmarks
make
```

### Build Individual

```bash
make virtual    # Virtual MIDI benchmark only
make driver     # MidiKit driver test only
```

### Clean

```bash
make clean          # Remove binaries
make clean-results  # Remove result files
make distclean      # Full clean
```

## Running Benchmarks

### Quick Start

```bash
# Run all benchmarks
make run-all

# Run specific benchmark
make run-virtual    # No hardware needed
make run-driver     # Requires APC Mini
```

### Manual Execution

```bash
# Virtual benchmark (always works)
./virtual_midi_benchmark

# Driver test (requires APC Mini connected)
./midikit_driver_test
```

Results are automatically saved to `results/` with timestamps.

## Interpreting Results

### Virtual MIDI Benchmark

**Good results**:
- Latency < 500 μs
- Throughput > 2,000 msg/sec
- 0 lost messages

**Poor results**:
- Latency > 1 ms (indicates system load)
- Throughput < 1,000 msg/sec
- Lost messages (indicates bugs)

### MidiKit Driver Test

**Success indicators**:
- Finds endpoints in BMidiRoster
- Completes all 10 batches
- Batch time < 100 ms

**Failure indicators**:
- Empty BMidiRoster (driver bug)
- "Kill Thread" crash (driver bug)
- Batch time > 1 second (blocking)

## Known Issues

### 1. BMidiRoster Empty

**Symptom**: `NextEndpoint()` returns no devices despite `/dev/midi/usb/0-0` existing

**Cause**: Haiku's `usb_midi` driver creates device files but doesn't register endpoints with BMidiRoster

**Workaround**: Direct `/dev/midi/usb/` access with BMidiPort

### 2. Driver Crashes on Batch Writes

**Symptom**: "Kill Thread" error when sending many MIDI messages rapidly

**Cause**: Thread safety bug in `midi_usb` driver or BMidiPort

**Impact**: Confirms blocking/concurrency issues in driver

**Workaround**: Use USB Raw access (main APC Mini app approach)

### 3. MidiKit Overhead

**Finding**: Even virtual MIDI routing has ~270 μs latency

**Impact**: Hardware MIDI will be slower than expected

**Baseline**: Any USB MIDI implementation will have MidiKit overhead + USB latency

## Comparison with Main Project

This benchmark suite provides evidence for architectural decisions in the main APC Mini project:

| Approach | Pros | Cons | Performance |
|----------|------|------|-------------|
| **MidiKit (tested here)** | Standard API | Driver bugs, overhead | ~270 μs + crashes |
| **USB Raw (main app)** | Bypasses bugs | Custom code | Direct, stable |

The benchmarks **prove** that USB Raw access was the correct choice for the main application.

## Contributing Results

When reporting benchmark results, please include:

1. **System info**: `uname -a`
2. **Haiku version**: `uname -v`
3. **Hardware**: USB MIDI device model
4. **Full output**: Save to `results/` directory
5. **Context**: System load, other MIDI apps running

## Related Documentation

- **Main project**: `../README.md`
- **Results analysis**: `RESULTS.md`
- **USB synchronization**: `../USB_SYNCHRONIZATION_SOLUTION.md`

## License

Same as main APC Mini project.

## Authors

Created as part of the APC Mini Test Application for Haiku OS project to document MIDI subsystem performance and justify architectural decisions.
