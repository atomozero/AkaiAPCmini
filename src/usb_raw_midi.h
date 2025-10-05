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