# Testing Plan for USB Synchronization Fix

## Overview

This document outlines the testing strategy for the new atomic + semaphore synchronization solution that replaces the flawed volatile + delay approach.

## Changes Summary

### Removed (Incorrect Implementation)
- `volatile bool skip_reading` - Non-atomic flag with no memory barriers
- `bool batch_mode` - Redundant state flag
- `snooze(50000)` - Arbitrary 50ms delay in `BeginBatchWrite()`
- `LockWithTimeout(10000)` - Timeout-based lock acquisition

### Added (Correct Implementation)
- `int32 batch_write_active` - Atomic flag using Haiku's `atomic_set()`/`atomic_get()`
- `sem_id batch_write_sem` - Semaphore for thread coordination
- `BAutolock` RAII pattern in reader thread
- Double-check of atomic flag after lock acquisition

## Test Cases

### 1. Basic Functionality Test

**Objective:** Verify batch writes work without errors

**Procedure:**
```cpp
usb->BeginBatchWrite();
for (int i = 0; i < 64; i++) {
    usb->SetPadColor(i, LED_GREEN);
}
usb->EndBatchWrite();
```

**Expected Result:**
- All 64 pads turn green
- No error messages
- No deadlocks

**Success Criteria:** LEDs update correctly

---

### 2. Rapid Batch Toggle Test

**Objective:** Detect race conditions under rapid state changes

**Procedure:**
```cpp
for (int i = 0; i < 1000; i++) {
    usb->BeginBatchWrite();
    usb->SetPadColor(i % 64, LED_RED);
    usb->EndBatchWrite();
    snooze(500);  // 0.5ms between batches
}
```

**Expected Result:**
- No crashes or hangs
- Each pad update visible
- No "USB transfer failed" errors

**Success Criteria:** All iterations complete without error

---

### 3. Concurrent Read/Write Test

**Objective:** Verify reader thread properly pauses during batch writes

**Procedure:**
1. Start pressing pads on APC Mini continuously
2. Execute batch write operations
3. Monitor for dropped MIDI messages

**Test Code:**
```cpp
// In separate thread: continuously monitor received messages
std::atomic<int> received_count{0};
usb->SetMIDICallback([&](uint8_t status, uint8_t data1, uint8_t data2) {
    received_count++;
    printf("Received: %02X %02X %02X\n", status, data1, data2);
});

// Main thread: perform batch writes
for (int i = 0; i < 100; i++) {
    usb->BeginBatchWrite();
    usb->SetPadColor(i % 64, LED_YELLOW);
    usb->EndBatchWrite();
    snooze(10000);  // 10ms between batches
}

printf("Total messages received: %d\n", received_count.load());
```

**Expected Result:**
- MIDI messages received between batch operations
- No messages lost (verify by counting pad presses)
- No "USB read error" messages

**Success Criteria:** Message count matches physical button presses

---

### 4. Latency Measurement Test

**Objective:** Verify elimination of 50ms delay

**Procedure:**
```cpp
bigtime_t total_latency = 0;
const int iterations = 100;

for (int i = 0; i < iterations; i++) {
    bigtime_t start = system_time();
    usb->BeginBatchWrite();
    bigtime_t begin_latency = system_time() - start;

    usb->SetPadColor(i % 64, LED_GREEN);

    start = system_time();
    usb->EndBatchWrite();
    bigtime_t end_latency = system_time() - start;

    total_latency += begin_latency;
    printf("BeginBatchWrite latency: %lld μs\n", begin_latency);

    snooze(5000);  // 5ms between tests
}

printf("Average BeginBatchWrite latency: %lld μs\n", total_latency / iterations);
```

**Expected Result:**
- Old implementation: ~50000μs (50ms) average
- New implementation: <100μs average (when reader idle)
- Worst case: <100000μs (100ms, when reader in BulkTransfer)

**Success Criteria:** Average latency < 1000μs (1ms)

---

### 5. Multi-Core Cache Coherency Test

**Objective:** Verify atomic operations work across CPU cores

**Prerequisites:**
- Multi-core Haiku system (not single-core VM)
- CPU affinity control (if available)

**Procedure:**
```cpp
// Pin reader thread to core 0
// Pin main thread to core 1
// (Haiku API for CPU affinity: _kern_set_thread_cpu())

// Rapidly toggle batch flag
for (int i = 0; i < 10000; i++) {
    usb->BeginBatchWrite();
    usb->EndBatchWrite();
    // No delay - maximum stress
}
```

**Expected Result:**
- No crashes or hangs
- No corruption of internal state
- Both threads see consistent flag values

**Success Criteria:** Completes without error

---

### 6. Reader Thread Starvation Test

**Objective:** Ensure reader thread resumes after batch operations

**Procedure:**
```cpp
// Monitor reader thread activity
std::atomic<bool> reader_active{false};
usb->SetMIDICallback([&](uint8_t s, uint8_t d1, uint8_t d2) {
    reader_active = true;
});

// Perform batch write
usb->BeginBatchWrite();
for (int i = 0; i < 64; i++) {
    usb->SetPadColor(i, LED_RED);
}
usb->EndBatchWrite();

// Wait for reader to resume
snooze(100000);  // 100ms

// Press a pad on APC Mini
printf("Press any pad now...\n");
snooze(5000000);  // 5 seconds

if (reader_active) {
    printf("✓ Reader thread resumed successfully\n");
} else {
    printf("✗ Reader thread appears blocked!\n");
}
```

**Expected Result:**
- Reader thread processes MIDI messages after batch write
- `reader_active` becomes true when pad pressed

**Success Criteria:** Reader thread resumes within 100ms of `EndBatchWrite()`

---

### 7. Error Injection Test

**Objective:** Verify graceful handling of edge cases

**Test Cases:**

#### 7a. Semaphore Creation Failure
```cpp
// Simulate by manually setting batch_write_sem = -1 after construction
// Should print warning but continue functioning
```

#### 7b. Lock Acquisition Timeout
```cpp
// Create artificial deadlock scenario
// Verify no infinite hang occurs
```

#### 7c. USB Device Disconnection During Batch Write
```cpp
usb->BeginBatchWrite();
// Physically disconnect APC Mini
usb->SetPadColor(0, LED_GREEN);  // Should fail gracefully
usb->EndBatchWrite();  // Should not hang
```

**Success Criteria:** No crashes, appropriate error messages

---

### 8. Long-Duration Stability Test

**Objective:** Verify no resource leaks or cumulative errors

**Procedure:**
```cpp
// Run for 1 hour
bigtime_t end_time = system_time() + (60LL * 60 * 1000000);  // 1 hour
int iteration = 0;

while (system_time() < end_time) {
    usb->BeginBatchWrite();
    for (int i = 0; i < 64; i++) {
        usb->SetPadColor(i, iteration % 3);  // Cycle colors
    }
    usb->EndBatchWrite();

    snooze(1000000);  // 1 second between updates
    iteration++;
}

printf("Completed %d iterations\n", iteration);
```

**Expected Result:**
- No memory leaks (check with `ps -aux` or Haiku debugger)
- No semaphore leaks (check `/dev/ports`)
- Consistent performance throughout test

**Success Criteria:** Runs for full duration without degradation

---

## Debugging Tools

### 1. Thread State Inspection

```cpp
// Add to ReaderThreadLoop():
printf("Reader: batch_active=%d, lock_held=%d\n",
       atomic_get(&batch_write_active),
       endpoint_lock.IsLocked());
```

### 2. Semaphore State Check

```bash
# In Haiku terminal:
$ /bin/ls -l /dev/ports | grep batch_write_sem
```

### 3. USB Traffic Monitoring

```bash
# In Haiku terminal (if available):
$ usbmon -i 0
```

---

## Performance Benchmarks

### Baseline Measurements (Old Implementation)

| Operation | Latency (μs) | Notes |
|-----------|--------------|-------|
| BeginBatchWrite() | 50,000 | Fixed 50ms delay |
| EndBatchWrite() | <100 | Just flag clear |
| Single SetPadColor() | 200-500 | USB bulk transfer |

### Target Measurements (New Implementation)

| Operation | Latency (μs) | Notes |
|-----------|--------------|-------|
| BeginBatchWrite() (idle reader) | <100 | No delay, just atomic + lock |
| BeginBatchWrite() (busy reader) | <100,000 | Wait for BulkTransfer timeout |
| EndBatchWrite() | <100 | Atomic + semaphore release |
| Single SetPadColor() | 200-500 | Unchanged |

### Expected Improvement

- **50x faster** in typical case (reader idle)
- **Same performance** in worst case (reader busy)
- **No added latency** compared to theoretical minimum

---

## Regression Testing

### Ensure No Breakage of Existing Features

1. **Fader Mapping Test**
   ```bash
   $ cd build && make test
   ```
   - Verify fader CC mappings still correct (48-56)

2. **LED Pattern Test**
   ```bash
   $ ./led_patterns
   ```
   - Verify all LED patterns render correctly

3. **MIDI Monitor Test**
   ```bash
   $ ./midi_monitor
   ```
   - Press pads, move faders, verify all events logged

---

## Success Criteria Summary

### Must Pass
- All 8 test cases complete without crashes
- Average `BeginBatchWrite()` latency < 1ms (50x improvement)
- No reader thread starvation after batch operations
- 1-hour stability test completes without errors

### Should Pass
- Multi-core cache coherency test (if multi-core Haiku available)
- Error injection tests handle edge cases gracefully

### Nice to Have
- Zero message loss during concurrent read/write test
- Sub-100μs latency in optimal conditions

---

## Test Execution Checklist

- [ ] Compile on Haiku R1/beta5 or later
- [ ] Run on physical hardware (not VM) for multi-core test
- [ ] Connect real APC Mini device
- [ ] Execute tests 1-8 in order
- [ ] Document any failures with logs
- [ ] Verify regression tests pass
- [ ] Measure and compare latency benchmarks

---

## Known Limitations

1. **USB Timeout Dependency**
   - `BeginBatchWrite()` worst-case latency depends on USB stack timeout
   - Typically 100ms on Haiku, not configurable

2. **Single Batch Write at a Time**
   - Nested `BeginBatchWrite()` calls not supported
   - Would require recursive locking (not implemented)

3. **Reader Thread Priority**
   - Set to `B_NORMAL_PRIORITY`
   - Real-time applications may need `B_REAL_TIME_PRIORITY`

---

## Troubleshooting Guide

### Issue: BeginBatchWrite() hangs indefinitely

**Possible Causes:**
- Reader thread crashed while holding lock
- USB device disconnected mid-transfer

**Diagnosis:**
```bash
$ listthreads <pid>  # Check thread states
$ strace -p <pid>    # Check system calls
```

**Solution:**
- Add timeout to `endpoint_lock.Lock()` (not currently implemented)
- Consider using `endpoint_lock.LockWithTimeout(5000000)` (5 seconds)

### Issue: Messages dropped during batch operations

**Possible Causes:**
- Reader thread not resuming after `EndBatchWrite()`
- Semaphore not properly released

**Diagnosis:**
```cpp
// Add logging to ReaderThreadLoop():
printf("Reader resumed, batch_active=%d\n", atomic_get(&batch_write_active));
```

**Solution:**
- Verify `release_sem_etc()` called in `EndBatchWrite()`
- Check semaphore count: `get_sem_count(batch_write_sem, &count)`

### Issue: Multi-core test fails with inconsistent state

**Possible Causes:**
- Compiler optimization reordering atomic operations
- Missing memory barriers (shouldn't happen with Haiku atomics)

**Diagnosis:**
- Disassemble binary, check for `lock` prefix on x86
- Add `printf()` between atomic operations (acts as barrier)

**Solution:**
- Verify GCC uses `-march=native` or `-march=x86-64`
- Check `/boot/system/develop/headers/os/kernel/OS.h` atomic implementation

---

## Next Steps After Testing

1. **If All Tests Pass:**
   - Update `CLAUDE.md` to reference new synchronization approach
   - Remove old `USB_SYNCHRONIZATION_FIX.md` (outdated)
   - Update deployment scripts
   - Tag release (e.g., `v2.0-sync-fix`)

2. **If Tests Fail:**
   - Document failure mode
   - Add debug logging to identify root cause
   - Consider alternative approaches (e.g., suspend_thread, though not recommended)

3. **Performance Optimization:**
   - If latency targets not met, profile with `perf` or Haiku Debugger
   - Consider tuning reader thread snooze duration (currently 100μs)

---

## References

- Original issue discussion: `USB_SYNCHRONIZATION_FIX.md`
- Haiku atomic operations: `/boot/system/develop/headers/os/kernel/OS.h`
- Haiku semaphores: https://www.haiku-os.org/docs/api/group__kernel__sync.html
- BLocker documentation: https://www.haiku-os.org/docs/api/classBLocker.html
