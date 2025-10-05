// usb_raw_midi.h
// Direct USB Raw MIDI Access for Haiku OS
//
// PURPOSE:
// Provides direct USB communication bypassing Haiku's MIDI Kit 2 stack to avoid
// Inter-Process Communication (IPC) overhead inherent in the client-server architecture.
//
// PERFORMANCE CHARACTERISTICS:
// - Latency: ~50-100μs (direct USB transfer)
// - Throughput: Limited only by USB hardware bandwidth (~1-2ms per bulk transfer)
// - No IPC overhead, no context switches, no serialization
// - Direct kernel access via /dev/bus/usb/raw
//
// COMPARISON WITH MIDI KIT 2:
// - MIDI Kit 2: ~270μs per message (IPC overhead)
// - USB Raw: ~50-100μs per message (2.7-5.4x faster)
// - Batch operations: USB Raw can group multiple messages in single transfer
// - Real-time capable: USB Raw meets <100μs latency requirement for LED control
//
// ARCHITECTURE:
// - Reader thread: Continuous polling of USB IN endpoint
// - Writer thread: Main thread sends to USB OUT endpoint
// - Thread coordination: Cooperative pausing with semaphores (see THREAD_SAFETY.md)
// - Lock protection: BLocker protects USB endpoint access during batch operations
//
// TRADE-OFFS:
// + Performance: 2.7-5.4x lower latency than MIDI Kit 2
// + Real-time: Meets strict timing requirements for LED feedback
// + Throughput: No IPC bottleneck
// - Portability: Haiku-specific (usb_raw API)
// - Isolation: No protected memory between components
// - Routing: No cross-application MIDI routing
//
// See: docs/technical/MIDIKIT2_ARCHITECTURE.md for detailed analysis
// See: benchmarks/RESULTS.md for performance measurements
// See: docs/technical/THREAD_SAFETY.md for synchronization details

#ifndef USB_RAW_MIDI_H
#define USB_RAW_MIDI_H

#include "apc_mini_defs.h"
#include <OS.h>
#include <Locker.h>
#include <functional>

class USBRawMIDI {
public:
    typedef std::function<void(uint8_t status, uint8_t data1, uint8_t data2)> MIDICallback;

    USBRawMIDI();
    ~USBRawMIDI();

    // Device management
    APCMiniError Initialize();
    void Shutdown();
    bool IsConnected() const { return device_fd >= 0; }

    // MIDI communication
    APCMiniError SendMIDI(uint8_t status, uint8_t data1, uint8_t data2);
    APCMiniError SendSysEx(const uint8_t* data, size_t length);
    APCMiniError SendNoteOn(uint8_t note, uint8_t velocity);
    APCMiniError SendNoteOff(uint8_t note);
    APCMiniError SendControlChange(uint8_t controller, uint8_t value);
    APCMiniError SetPadColor(uint8_t pad, APCMiniLEDColor color);

    // Optimized batch operations
    // Sends multiple LED updates in a single operation with reader thread paused
    // Performance: ~30ms for 64 LEDs vs ~47ms with MIDI Kit 2 (36% faster)
    // Note: Automatically pauses reader thread during batch
    APCMiniError SetPadColorsBatch(const uint8_t* pads, const APCMiniLEDColor* colors, size_t count);

    // APC Mini MK2 initialization
    APCMiniError SendIntroductionMessage();

    // Callback registration
    void SetMIDICallback(MIDICallback callback) { midi_callback = callback; }

    // Reader thread control for batch operations
    void PauseReader();
    void ResumeReader();

    // Device detection
    static bool FindAPCMini(char* device_path, size_t path_size);

    // Statistics
    const APCMiniStats& GetStats() const { return stats; }
    void ResetStats();

private:
    // USB device management
    int device_fd;
    int interface_num;
    int endpoint_in;
    int endpoint_out;

    // Threading
    thread_id reader_thread;
    volatile bool should_stop;
    volatile bool pause_requested;
    volatile bool is_paused;
    sem_id pause_sem;            // Signals when pause is complete
    BLocker endpoint_lock;       // Synchronizes USB endpoint access

    // Callback
    MIDICallback midi_callback;

    // Statistics
    APCMiniStats stats;
    bigtime_t last_message_time;

    // USB operations
    APCMiniError OpenDevice(const char* device_path);
    void CloseDevice();
    APCMiniError ClaimInterface();
    APCMiniError FindEndpoints();

    // MIDI packet handling
    APCMiniError SendUSBMIDIPacket(const USBMIDIEventPacket& packet);
    void ProcessUSBMIDIPacket(const USBMIDIEventPacket& packet);

    // Threading
    static int32 ReaderThreadEntry(void* data);
    void ReaderThreadLoop();

    // Utilities
    void UpdateLatencyStats(bigtime_t latency);
    uint8_t CalculateUSBMIDIHeader(uint8_t cable, uint8_t status);
};

// USB Raw device discovery utilities
class USBDeviceScanner {
public:
    struct USBDevice {
        char path[256];
        uint16_t vendor_id;
        uint16_t product_id;
        char manufacturer[128];
        char product[128];
    };

    static int ScanUSBDevices(USBDevice* devices, int max_devices);
    static bool IsAPCMini(const USBDevice& device);
    static void PrintDeviceInfo(const USBDevice& device);
};

#endif // USB_RAW_MIDI_H