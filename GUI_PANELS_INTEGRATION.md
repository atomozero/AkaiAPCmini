# GUI Diagnostic Panels Integration Guide

## Overview

Added two diagnostic panels to the APC Mini GUI:

1. **ConnectionStatusPanel**: Shows USB Raw vs MIDI fallback connection status
2. **PerformanceIndicatorPanel**: Shows real-time latency and message statistics

## Files Modified

- `src/apc_mini_gui.h` - Added panel class declarations
- `src/apc_mini_gui_panels.cpp` - Panel implementations (NEW FILE)
- `build/Makefile` - Added apc_mini_gui_panels.cpp to build

## Integration Steps

### 1. Initialize Panels in APCMiniWindow Constructor

In `src/apc_mini_gui_app.cpp`, add to `APCMiniWindow::APCMiniWindow()` constructor:

```cpp
// After creating other views, add:

// Create diagnostic panels
BRect status_rect(10, window_height - 120, 300, window_height - 10);
connection_panel = new ConnectionStatusPanel(status_rect);
AddChild(connection_panel);

BRect perf_rect(310, window_height - 120, 600, window_height - 10);
performance_panel = new PerformanceIndicatorPanel(perf_rect);
AddChild(performance_panel);
```

### 2. Update Connection Status

In `APCMiniGUIApp::InitializeHardware()`:

```cpp
bool APCMiniGUIApp::InitializeHardware()
{
    // Existing USB Raw initialization...
    usb_midi = new USBRawMIDI();

    if (usb_midi->Initialize()) {
        // Success - using USB Raw
        if (main_window && main_window->connection_panel) {
            main_window->connection_panel->UpdateStatus(true, false); // USB connected, not fallback
            main_window->connection_panel->SetDeviceInfo("APC Mini mk2", 0x09E8, 0x0028);
        }
        return true;
    }

    // Try MIDI fallback...
    if (/* MIDI fallback succeeds */) {
        if (main_window && main_window->connection_panel) {
            main_window->connection_panel->UpdateStatus(true, true); // Connected but using fallback
            main_window->connection_panel->SetDeviceInfo("APC Mini (MIDI Fallback)", 0, 0);
        }
        return true;
    }

    // No connection
    if (main_window && main_window->connection_panel) {
        main_window->connection_panel->UpdateStatus(false, false);
    }
    return false;
}
```

### 3. Record Performance Metrics

#### For Message Sending

In `APCMiniGUIApp::SendNoteOn()` and similar methods:

```cpp
void APCMiniGUIApp::SendNoteOn(uint8_t note, uint8_t velocity)
{
    bigtime_t start_time = system_time();

    // Existing send code...
    usb_midi->SendNoteOn(note, velocity);

    bigtime_t latency = system_time() - start_time;

    if (main_window && main_window->performance_panel) {
        main_window->performance_panel->RecordLatency(latency);
        main_window->performance_panel->IncrementMessageCount(true); // sent
    }
}
```

#### For Message Receiving

In `APCMiniGUIApp::HandleMIDIMessage()`:

```cpp
void APCMiniGUIApp::HandleMIDIMessage(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (main_window && main_window->performance_panel) {
        main_window->performance_panel->IncrementMessageCount(false); // received
    }

    // Existing handler code...
}
```

### 4. Update Panel Display

The panels auto-update via `Pulse()` method (called every 100-500ms).

To manually force an update:

```cpp
if (main_window->connection_panel) {
    main_window->connection_panel->Invalidate();
}
if (main_window->performance_panel) {
    main_window->performance_panel->Invalidate();
}
```

### 5. Reset Statistics (Optional)

When resetting device or reconnecting:

```cpp
if (main_window && main_window->performance_panel) {
    main_window->performance_panel->ResetStatistics();
}
```

## Panel Features

### ConnectionStatusPanel

**Shows**:
- Connection status (Connected/Disconnected)
- Mode (USB Raw / MIDI Fallback)
- Device info (Name, VID, PID)
- Performance expectation based on mode

**Color coding**:
- 🟢 Green: USB Raw (optimal, <1ms latency)
- 🟡 Yellow: MIDI Fallback (degraded, ~5ms latency)
- 🔴 Red: Disconnected

### PerformanceIndicatorPanel

**Shows**:
- Current and average latency in microseconds
- Message counts (TX/RX)
- Throughput in messages per second

**Color coding** (latency):
- 🟢 Green: <100μs (excellent)
- 🟡 Yellow: 100-1000μs (good)
- 🟠 Orange: 1-5ms (acceptable)
- 🔴 Red: >5ms (poor)

## Visual Layout

Panels are positioned at the bottom of the window:

```
┌─────────────────────────────────────────────┐
│  Main APC Mini GUI                          │
│  (Pads, Faders, Buttons)                    │
│                                             │
│  ...                                        │
│                                             │
├─────────────────┬───────────────────────────┤
│ Connection      │ Performance               │
│ Status Panel    │ Indicator Panel           │
│                 │                           │
│ ✓ Connected     │ Latency: 42 μs (avg 35μs)│
│ Mode: USB Raw   │ Messages: TX:1234 RX:567 │
│ Device: APC Mini│ Throughput: 892 msg/s    │
│ Perf: Excellent │                           │
└─────────────────┴───────────────────────────┘
```

## Compilation

The new file is already added to the Makefile. Simply rebuild:

```bash
cd build
make clean
make gui
```

## Testing Checklist

- [ ] Panels visible at bottom of window
- [ ] Connection status updates on device connect/disconnect
- [ ] Mode correctly shows USB Raw vs MIDI Fallback
- [ ] Latency values update in real-time
- [ ] Message counts increment correctly
- [ ] Throughput calculation shows msg/s
- [ ] Color coding changes based on performance
- [ ] Panels update via Pulse() without manual calls

## Benefits

✅ **Real-time diagnostics** - See connection mode instantly
✅ **Performance validation** - Verify <1ms USB Raw latency vs ~5ms MIDI fallback
✅ **Message tracking** - Debug message flow issues
✅ **Visual feedback** - Color-coded status at a glance
✅ **Benchmark integration** - Uses same metrics as benchmark suite

This provides visual confirmation of the architectural advantages documented in the benchmark findings!
