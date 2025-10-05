# MidiKit Driver Test Documentation

## Purpose

Test whether the blocking behavior during batch LED writes occurs in:
- **Haiku's midi_usb driver** (BUSBEndpoint::BulkTransfer level), or
- **Application-level code** (usb_raw_midi.cpp implementation)

## Test Architecture

### What This Test Does

```
┌────────────────────────────────────────┐
│  midikit_driver_test                   │
│  - Pure MidiKit API (BMidiProducer)    │
│  - No USB Raw access                   │
│  - Bypasses usb_raw_midi.cpp           │
└──────────┬─────────────────────────────┘
           │
           │ BMidiProducer::SprayNoteOn()
           ▼
┌──────────────────────────────────────┐
│   Haiku MidiKit (BMidi API)          │  ← Thin layer
└──────────┬───────────────────────────┘
           │
           ▼
┌──────────────────────────────────────┐
│   Haiku midi_usb Driver              │  ← THE ACTUAL TEST TARGET
│   - BUSBEndpoint::BulkTransfer()     │
│   - Bulk transfer management         │
│   - Thread safety/locking logic      │
└──────────┬───────────────────────────┘
           │
           ▼
┌──────────────────────────────────────┐
│   USB Stack (kernel level)           │
└──────────────────────────────────────┘
```

### What Application Test Does (Comparison)

```
┌────────────────────────────────────────┐
│  apc_mini_test / apc_mini_gui          │
└──────────┬─────────────────────────────┘
           │
           ▼
┌──────────────────────────────────────┐
│   usb_raw_midi.cpp                   │  ← Application-level code
│   - Custom thread management         │
│   - Custom synchronization           │
│   - ioctl() to USB Raw API           │
└──────────┬───────────────────────────┘
           │
           │ B_USB_RAW_COMMAND_BULK_TRANSFER
           ▼
┌──────────────────────────────────────┐
│   USB Stack (kernel level)           │  ← Bypasses midi_usb driver
└──────────────────────────────────────┘
```

## Test Methodology

### Test Configuration
- **Batch size**: 64 LED commands (full pad grid)
- **Iterations**: 10 batches
- **Timeout threshold**: 5 seconds (indicates blocking)
- **Expected time**: 1-2ms per batch (fast USB bulk transfers)

### Test Procedure

1. **Connect** to APC Mini via MidiKit API only
2. **Send** batches of 64 LED commands using `BMidiProducer::SprayNoteOn()`
3. **Measure** time for each batch completion
4. **Detect** if any batch exceeds timeout (>5 seconds)
5. **Analyze** timing patterns to identify blocking

### Metrics Collected

- Messages sent count
- Batches completed
- Min/Avg/Max batch time (microseconds)
- Timeout count (batches exceeding 5 seconds)

## Building and Running

**Note**: This test is now part of the independent benchmark suite.

### Build on Haiku
```bash
cd benchmarks
make driver
```

### Run Test
```bash
cd benchmarks
./midikit_driver_test
```

**Note**: Test requires:
- APC Mini connected via USB
- Haiku OS with MidiKit support
- midi_usb driver loaded

## Interpreting Results

### Scenario 1: Blocking Detected in Driver Test

```
Batch timing:
  Min:    850 μs
  Avg: 95,000 μs  ← >10ms indicates driver blocking
  Max: 150,000 μs

⚠ BLOCKING DETECTED: 8 batches exceeded timeout
  -> This suggests blocking occurs in Haiku midi_usb driver
  -> Driver may be holding lock during BulkTransfer()
```

**Interpretation**:
- Problem is in **Haiku's midi_usb driver**
- BUSBEndpoint::BulkTransfer() likely blocks while holding endpoint lock
- Application-level code (usb_raw_midi.cpp) is not the cause

**Solution**:
- Fix belongs in Haiku's midi_usb driver source
- Driver needs to release lock before blocking BulkTransfer
- See USB_SYNCHRONIZATION_SOLUTION.md for details

### Scenario 2: No Blocking in Driver Test

```
Batch timing:
  Min:   800 μs
  Avg: 1,200 μs  ← Fast, within expected range
  Max: 2,500 μs

✓ No blocking detected
  -> midi_usb driver handles concurrent operations correctly
  -> Blocking issue (if any) is in application-level code
```

**Interpretation**:
- **Haiku's midi_usb driver works correctly**
- Blocking (if present) is in usb_raw_midi.cpp implementation
- Thread safety issue is application-level

**Solution**:
- Fix belongs in src/usb_raw_midi.cpp or src/usb_haiku_midi.cpp
- Review synchronization logic in application code
- Check BeginBatchWrite/EndBatchWrite implementation

### Scenario 3: Moderate Delays (10-50ms)

```
Batch timing:
  Min:  5,000 μs
  Avg: 25,000 μs  ← Moderate delay
  Max: 48,000 μs

⚠ Slower than expected (>10ms)
  -> May indicate some driver-level queuing/blocking
```

**Interpretation**:
- Partial blocking or queuing in midi_usb driver
- Not severe timeout but noticeable delay
- May indicate lock contention between read/write operations

**Solution**:
- Investigate both driver and application levels
- Check if multiple applications are accessing MIDI device
- Review USB transfer scheduling in driver

## Expected Timing Benchmarks

### Ideal Performance (No Blocking)
- Single MIDI message: ~10-50 μs
- Batch of 64 messages: ~1-2 ms
- 10 batches (640 messages): ~10-20 ms total

### Problematic Performance (Driver Blocking)
- Batch blocked by reader thread: 50-100 ms (USB timeout duration)
- Multiple batches blocked: >500 ms total
- Timeout errors or complete hangs

## Comparison with Application Test

Run both tests to isolate the problem:

### Test 1: MidiKit Driver Test (this test)
```bash
./midikit_driver_test
```
- Tests **midi_usb driver** behavior
- Pure MidiKit API path

### Test 2: Application Test (USB Raw)
```bash
./apc_mini_gui
# or
./apc_mini_test_debug
```
- Tests **application-level** code behavior
- USB Raw API path (bypasses midi_usb driver)

### Decision Matrix

| Driver Test | App Test | Conclusion |
|-------------|----------|------------|
| Blocking    | Blocking | Problem in **USB stack** (kernel level) |
| Blocking    | No block | Problem in **midi_usb driver** |
| No block    | Blocking | Problem in **application code** (usb_raw_midi.cpp) |
| No block    | No block | No problem (issue may be environmental) |

## Known Issues and Limitations

### Limitation 1: MidiKit API Differences
- `BMidiProducer::SprayNoteOn()` may have different buffering than USB Raw
- Driver may queue messages differently than raw ioctl()
- Results should be interpreted as driver-level behavior, not absolute performance

### Limitation 2: Device State
- APC Mini may behave differently depending on previous state
- Ensure device is reset before testing
- Cold boot may produce different results than warm restart

### Limitation 3: System Load
- Other MIDI applications may interfere
- System load affects timing measurements
- Run test on idle system for accurate results

## Related Documentation

- **USB_SYNCHRONIZATION_SOLUTION.md**: Detailed analysis of blocking problem
- **THREAD_SAFETY_ANALYSIS.md**: Thread safety review of application code
- **USB_SYNCHRONIZATION_FIX.md**: Original problem description and attempted fixes
- **TESTING_SYNCHRONIZATION_FIX.md**: Test methodology for synchronization fixes

## Test Code Location

**Note**: This test is part of the separate benchmark suite.

- **Source**: `benchmarks/src/midikit_driver_test.cpp`
- **Makefile target**: `cd benchmarks && make driver`
- **Binary**: `benchmarks/midikit_driver_test`
- **Documentation**: See `benchmarks/README.md` for full details

## Author Notes

This test was created to isolate whether the blocking behavior documented in USB_SYNCHRONIZATION_SOLUTION.md occurs at the:
- **Driver level** (Haiku's midi_usb using BUSBEndpoint::BulkTransfer), or
- **Application level** (usb_raw_midi.cpp using ioctl bulk transfers)

The distinction is critical because:
- **Driver-level problems** require fixes in Haiku's source tree
- **Application-level problems** can be fixed in this project's code

By testing with pure MidiKit API (bypassing our application code), we can definitively determine where the "ciccia" (meat) of the problem lies.
