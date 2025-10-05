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
./virtual_midi_benchmark [options]

# Options:
#   --verbose, -v       Show histogram and detailed statistics
#   --debug, -d         Enable debug output
#   --json [file]       Export results to JSON
#   --csv [file]        Export results to CSV
#   --batch-opt         Run batch size optimization test
#   --help, -h          Show help
```

**Typical results**:
- Latency: ~6 μs per message (min/p50/avg/p95/p99/max)
- Standard deviation: ~2 μs (low jitter)
- Throughput: ~17,000 msg/sec
- Batch 64 msgs: ~11 ms
- Reliability: 100% (0 lost messages)
- Optimal batch size: 64-128 messages

**Key finding**: MidiKit has minimal overhead for virtual routing (~6 μs avg). The MIDI Kit 2 client-server architecture provides efficient Inter-Process Communication (IPC) for MIDI messages. USB/hardware adds additional latency on top of this baseline.

**Architecture Details**: MIDI Kit 2 uses a client-server model with a centralized `midi_server` process:
- **Client-Server Model**: Each application communicates with `midi_server` via IPC
- **Message Pipeline**: App → libmidi2 → midi_server → libmidi2 → Target App
- **IPC Overhead**: ~6μs per message (efficient implementation)
- **Proxy Objects**: Each endpoint accessed through proxy
- **Protected Memory**: Full address space isolation between applications
- **No Batching**: Each MIDI message sent individually

**Performance Characteristics**:
- Low latency: ~6μs average
- High throughput: ~17k msg/sec
- Efficient routing: Minimal overhead
- Protected memory: Safe inter-app communication

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
- **BMidiRoster wrong names**: Endpoints named `/dev/midi/usb/0-0` instead of device name
- **Crashes without workaround**: "Kill Thread" error during rapid batch writes
- **Race condition confirmed**: Requires 5ms delay between messages to avoid crash
- **160x performance penalty**: Workaround makes batch operations 160x slower than expected

---

### 3. Raw Driver Benchmark

**Purpose**: Bypass libmidi completely and test driver performance directly

**What it tests**:
- Direct `/dev/midi/usb/` file I/O (no MIDI Kit libraries)
- Raw `write()` system calls to driver
- Driver race condition detection
- Performance without IPC overhead

**Hardware required**: APC Mini (or compatible USB MIDI device)

**Run**:
```bash
make raw
./raw_driver_benchmark [options]

# Options:
#   --verbose, -v    Show detailed output
#   --debug, -d      Show per-message debug info
#   --quiet, -q      Minimal output
```

**Architecture**:
- **No libmidi**: Bypasses all MIDI Kit libraries
- **No midi_server**: No IPC overhead
- **Direct driver**: `App → write() → midi_usb driver → USB`

**Test scenarios**:
1. **No delay**: Sends messages as fast as possible (tests race condition)
2. **1ms delay**: Minimal delay between messages
3. **5ms delay**: Known-safe delay from libmidi tests

**Expected results**:
- **If crashes with no delay**: Confirms driver race condition exists even without libmidi
- **If works with no delay**: Problem is in libmidi overhead, not driver
- **Performance comparison**: Shows true driver capability vs libmidi overhead

**Key finding**: This test isolates whether the performance bottleneck and race condition are in:
- **libmidi/libmidi2** (IPC, message handling)
- **midi_usb driver** (kernel driver itself)

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
make raw        # Raw driver benchmark only
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
make run-raw        # Requires APC Mini
```

### Manual Execution

```bash
# Virtual benchmark (always works)
./virtual_midi_benchmark

# Driver test (requires APC Mini connected)
./midikit_driver_test

# Raw driver test (requires APC Mini connected)
./raw_driver_benchmark
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

**Cause**: Race condition in `midi_usb` driver - severe thread safety bug

**Testing results**:
- Without delay: Crashes immediately
- With 1ms delay: Still crashes
- With 5ms delay: Works but 160x slower (~320ms vs ~2ms expected)

**Impact**: Driver requires 5ms minimum delay between messages, making it unusable for real-time applications

**Workaround**: Use USB Raw access (main APC Mini app approach)

### 3. BMidiPort API Differences

**Finding**: BMidiPort uses 1-based channel numbering (1-16), not 0-based (0-15)

**Impact**: Code using raw MIDI conventions (channel 0-15) must add +1 for BMidiPort

**Fixed in**: midikit_driver_test.cpp uses `APC_MINI_MIDI_CHANNEL + 1` for BMidiPort

## Comparison with Main Project

This benchmark suite provides evidence for architectural decisions in the main APC Mini project:

| Approach | Pros | Cons | Performance |
|----------|------|------|-------------|
| **MidiKit 2 (virtual)** | Standard API, IPC | Server overhead | ~7.65 μs (virtual only) |
| **MidiKit 1 (BMidiPort)** | Direct driver | Driver bugs, crashes | ~5000 μs + crashes |
| **Raw driver (this test)** | No libmidi overhead | Still uses midi_usb | TBD (run test) |
| **USB Raw (main app)** | Bypasses all | Custom code | Direct, stable |

The benchmarks **prove** that USB Raw access was the correct choice for the main application.

**Raw driver test purpose**: Determine if the bottleneck is in libmidi (IPC overhead) or midi_usb driver itself.

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
