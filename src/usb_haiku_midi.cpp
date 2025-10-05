#include "usb_raw_midi.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

// Haiku native USB headers
#include <os/device/USBKit.h>
#include <os/drivers/usb/USB_midi.h>

// Haiku synchronization headers
#include <support/Autolock.h>

// Haiku native USB MIDI implementation for APC Mini MK2

class APCMiniUSBDevice : public BUSBDevice {
public:
    APCMiniUSBDevice(const char* path) : BUSBDevice(path), endpoint_in(nullptr), endpoint_out(nullptr) {}

    bool IsAPCMini() {
        return (VendorID() == APC_MINI_VENDOR_ID &&
                (ProductID() == APC_MINI_PRODUCT_ID || ProductID() == APC_MINI_MK2_PRODUCT_ID));
    }

    status_t FindMIDIEndpoints() {
        // Find MIDI endpoints for bulk transfer
        const BUSBConfiguration* config = ConfigurationAt(0);
        if (!config) return B_ERROR;

        const BUSBInterface* interface = config->InterfaceAt(0);
        if (!interface) return B_ERROR;

        // Look for bulk endpoints (MIDI uses bulk transfers)
        for (uint32 i = 0; i < interface->CountEndpoints(); i++) {
            const BUSBEndpoint* endpoint = interface->EndpointAt(i);
            if (!endpoint) continue;

            if (endpoint->IsBulk()) {
                if (endpoint->IsInput()) {
                    endpoint_in = const_cast<BUSBEndpoint*>(endpoint);
                } else {
                    endpoint_out = const_cast<BUSBEndpoint*>(endpoint);
                }
            }
        }

        return (endpoint_in && endpoint_out) ? B_OK : B_ERROR;
    }

    BUSBEndpoint* endpoint_in;
    BUSBEndpoint* endpoint_out;
};

class APCMiniUSBRoster : public BUSBRoster {
public:
    APCMiniUSBRoster() : found_device(nullptr), endpoint_in(nullptr), endpoint_out(nullptr) {}

    virtual status_t DeviceAdded(BUSBDevice* device) {
        // Check if this is an APC Mini device
        if (device->VendorID() == APC_MINI_VENDOR_ID &&
            (device->ProductID() == APC_MINI_PRODUCT_ID || device->ProductID() == APC_MINI_MK2_PRODUCT_ID)) {

            printf("   🎹 Found APC Mini device: VID=%04X PID=%04X Location=%s\n",
                   device->VendorID(), device->ProductID(), device->Location());
            found_device = device;

            // Find MIDI endpoints
            if (FindMIDIEndpoints(device) == B_OK) {
                printf("   ✓ APC Mini MK2 hardware detected successfully\n");
                return B_OK;
            } else {
                printf("   ⚠️  APC Mini found but MIDI endpoints missing\n");
                found_device = nullptr;
            }
        }
        return B_ERROR; // Don't keep non-APC devices
    }

    virtual void DeviceRemoved(BUSBDevice* device) {
        if (found_device == device) {
            printf("   ⚠️  APC Mini device disconnected\n");
            found_device = nullptr;
            endpoint_in = nullptr;
            endpoint_out = nullptr;
        }
    }

    status_t FindMIDIEndpoints(BUSBDevice* device) {
        // Find MIDI endpoints for interrupt or bulk transfer
        const BUSBConfiguration* config = device->ConfigurationAt(0);
        if (!config) return B_ERROR;

        printf("   🔍 Analyzing USB device interfaces (%u found)\n", config->CountInterfaces());

        // USB MIDI devices typically have:
        // Interface 0: Audio Control Interface
        // Interface 1: MIDI Streaming Interface (with endpoints)
        for (uint32 intf_idx = 0; intf_idx < config->CountInterfaces(); intf_idx++) {
            const BUSBInterface* interface = config->InterfaceAt(intf_idx);
            if (!interface) continue;

            printf("     Interface %u: Class=%02X Subclass=%02X Protocol=%02X Endpoints=%u\n",
                   intf_idx, interface->Class(), interface->Subclass(),
                   interface->Protocol(), interface->CountEndpoints());

            // Skip interfaces with no endpoints
            if (interface->CountEndpoints() == 0) continue;

            // Look for interrupt or bulk endpoints (USB MIDI can use either)
            for (uint32 i = 0; i < interface->CountEndpoints(); i++) {
                const BUSBEndpoint* endpoint = interface->EndpointAt(i);
                if (!endpoint) continue;

                printf("       Endpoint %u: Bulk=%d Interrupt=%d Input=%d Output=%d\n",
                       i, endpoint->IsBulk(), endpoint->IsInterrupt(),
                       endpoint->IsInput(), endpoint->IsOutput());

                // Check for both interrupt and bulk endpoints
                if (endpoint->IsInterrupt() || endpoint->IsBulk()) {
                    if (endpoint->IsInput()) {
                        endpoint_in = const_cast<BUSBEndpoint*>(endpoint);
                        printf("         ✓ Found input endpoint (type: %s)\n",
                               endpoint->IsInterrupt() ? "interrupt" : "bulk");
                    } else {
                        endpoint_out = const_cast<BUSBEndpoint*>(endpoint);
                        printf("         ✓ Found output endpoint (type: %s)\n",
                               endpoint->IsInterrupt() ? "interrupt" : "bulk");
                    }
                }
            }

            // If we found both endpoints, we're done
            if (endpoint_in && endpoint_out) break;
        }

        return (endpoint_in && endpoint_out) ? B_OK : B_ERROR;
    }

    BUSBDevice* found_device;
    BUSBEndpoint* endpoint_in;
    BUSBEndpoint* endpoint_out;
};

// Global roster for device detection
static APCMiniUSBRoster* g_usb_roster = nullptr;

USBRawMIDI::USBRawMIDI()
    : device_fd(-1)
    , interface_num(-1)
    , endpoint_in(0)
    , endpoint_out(0)
    , reader_thread(-1)
    , should_stop(false)
    , pause_requested(false)
    , is_paused(false)
    , pause_sem(-1)
    , endpoint_lock("usb_endpoint")
    , last_message_time(0)
{
    memset(&stats, 0, sizeof(stats));
    stats.min_latency_us = UINT32_MAX;

    // Create semaphore for pause synchronization
    pause_sem = create_sem(0, "usb_pause_sem");
}

USBRawMIDI::~USBRawMIDI()
{
    Shutdown();

    // Clean up semaphore
    if (pause_sem >= 0) {
        delete_sem(pause_sem);
        pause_sem = -1;
    }
}

APCMiniError USBRawMIDI::Initialize()
{
    printf("🔌 Initializing USB Hardware Detection...\n");

    // Create USB roster to detect APC Mini
    g_usb_roster = new APCMiniUSBRoster();
    g_usb_roster->Start();

    // Wait for device detection (USB enumeration can take time)
    printf("   ⏳ Scanning USB devices for APC Mini hardware...\n");
    for (int i = 0; i < 10 && !g_usb_roster->found_device; i++) {
        snooze(100000); // 100ms per iteration, up to 1 second total
    }

    if (!g_usb_roster->found_device) {
        printf("APC Mini not found via USB Kit\n");
        delete g_usb_roster;
        g_usb_roster = nullptr;
        return APC_ERROR_DEVICE_NOT_FOUND;
    }

    printf("   ✅ APC Mini MK2 connected successfully!\n");
    printf("   📡 MIDI endpoints active: IN=%p OUT=%p\n",
           g_usb_roster->endpoint_in,
           g_usb_roster->endpoint_out);

    // Start reader thread with higher priority for ultra-low latency
    reader_thread = spawn_thread(ReaderThreadEntry, "apc_usb_reader",
                                B_URGENT_DISPLAY_PRIORITY, this);
    if (reader_thread >= 0) {
        resume_thread(reader_thread);
    }

    device_fd = 1; // Mark as connected
    return APC_SUCCESS;
}

void USBRawMIDI::Shutdown()
{
    should_stop = true;

    if (reader_thread >= 0) {
        status_t exit_value;
        wait_for_thread(reader_thread, &exit_value);
        reader_thread = -1;
    }

    if (g_usb_roster) {
        g_usb_roster->Stop();
        delete g_usb_roster;
        g_usb_roster = nullptr;
    }

    device_fd = -1;
    printf("USB MIDI connection closed\n");
}

APCMiniError USBRawMIDI::SendMIDI(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!IsConnected() || !g_usb_roster || !g_usb_roster->found_device) {
        return APC_ERROR_USB_TRANSFER_FAILED;
    }

    // Create USB MIDI packet
    usb_midi_event_packet packet;
    packet.cn = 0;  // Cable number 0

    // Determine Code Index Number based on MIDI message type
    uint8_t msg_type = status & 0xF0;
    switch (msg_type) {
        case MIDI_NOTE_OFF:
            packet.cin = 0x8;
            break;
        case MIDI_NOTE_ON:
            packet.cin = 0x9;
            break;
        case MIDI_CONTROL_CHANGE:
            packet.cin = 0xB;
            break;
        default:
            packet.cin = 0x9; // Default to note on
            break;
    }

    packet.midi[0] = status;
    packet.midi[1] = data1;
    packet.midi[2] = data2;

    // Acquire lock for exclusive endpoint access
    BAutolock auto_lock(endpoint_lock);
    if (!auto_lock.IsLocked()) {
        return APC_ERROR_USB_TRANSFER_FAILED;
    }

    // Send via USB endpoint
    BUSBEndpoint* endpoint = g_usb_roster->endpoint_out;
    APCMiniError result_code = APC_ERROR_USB_TRANSFER_FAILED;

    if (endpoint) {
        ssize_t result;

        // Use appropriate transfer method based on endpoint type
        if (endpoint->IsInterrupt()) {
            result = endpoint->InterruptTransfer(&packet, sizeof(packet));
        } else {
            result = endpoint->BulkTransfer(&packet, sizeof(packet));
        }

        if (result == sizeof(packet)) {
            stats.messages_sent++;
            result_code = APC_SUCCESS;
        } else {
            printf("USB MIDI send failed: %s (sent %zd bytes)\n",
                   strerror(result < 0 ? result : B_ERROR), result < 0 ? 0 : result);
            stats.error_count++;
        }
    }

    return result_code;
}

APCMiniError USBRawMIDI::SendSysEx(const uint8_t* data, size_t length)
{
    if (!g_usb_roster || !g_usb_roster->found_device) {
        return APC_ERROR_DEVICE_NOT_FOUND;
    }

    BUSBEndpoint* endpoint = g_usb_roster->endpoint_out;
    if (!endpoint) {
        return APC_ERROR_DEVICE_NOT_FOUND;
    }

    // Acquire lock for exclusive endpoint access
    BAutolock auto_lock(endpoint_lock);
    if (!auto_lock.IsLocked()) {
        return APC_ERROR_USB_TRANSFER_FAILED;
    }

    // Send SysEx as USB MIDI packets (4 bytes each)
    // CIN values: 0x04 = SysEx start/continue, 0x05/0x06/0x07 = SysEx end with 1/2/3 bytes
    size_t offset = 0;
    APCMiniError result_code = APC_SUCCESS;

    while (offset < length && result_code == APC_SUCCESS) {
        usb_midi_event_packet packet = {0};
        size_t remaining = length - offset;

        if (remaining >= 3) {
            // Full packet (3 MIDI bytes)
            if (offset + 3 < length) {
                // Not the last packet
                packet.cin = 0x04;  // SysEx starts or continues
            } else {
                // Last packet with 3 bytes
                packet.cin = 0x07;  // SysEx ends with 3 bytes
            }
            packet.midi[0] = data[offset];
            packet.midi[1] = data[offset + 1];
            packet.midi[2] = data[offset + 2];
            offset += 3;
        } else if (remaining == 2) {
            // Last packet with 2 bytes
            packet.cin = 0x06;  // SysEx ends with 2 bytes
            packet.midi[0] = data[offset];
            packet.midi[1] = data[offset + 1];
            packet.midi[2] = 0;
            offset += 2;
        } else {
            // Last packet with 1 byte
            packet.cin = 0x05;  // SysEx ends with 1 byte
            packet.midi[0] = data[offset];
            packet.midi[1] = 0;
            packet.midi[2] = 0;
            offset += 1;
        }

        // Send packet
        ssize_t result = endpoint->BulkTransfer(&packet, sizeof(packet));
        if (result != sizeof(packet)) {
            printf("USB SysEx packet send failed: %s\n",
                   strerror(result < 0 ? result : B_ERROR));
            result_code = APC_ERROR_USB_TRANSFER_FAILED;
            stats.error_count++;
        } else {
            stats.messages_sent++;
        }
    }

    return result_code;
}

APCMiniError USBRawMIDI::SendIntroductionMessage()
{
    // APC Mini MK2 Introduction Message (from protocol doc page 13)
    // F0 47 7F 4F 60 00 04 00 <Version High> <Version Low> <Bugfix> F7
    const uint8_t intro_msg[] = {
        0xF0,  // SysEx start
        0x47,  // Manufacturer ID (Akai)
        0x7F,  // Device ID
        0x4F,  // Product ID (APC Mini MK2)
        0x60,  // Message type: Introduction
        0x00,  // Data length MSB
        0x04,  // Data length LSB (4 bytes)
        0x00,  // Application/Configuration ID
        0x01,  // Version High
        0x00,  // Version Low
        0x00,  // Bugfix level
        0xF7   // SysEx end
    };

    printf("   📤 Sending Introduction Message to APC Mini MK2...\n");
    APCMiniError result = SendSysEx(intro_msg, sizeof(intro_msg));

    if (result == APC_SUCCESS) {
        printf("   ✅ Introduction Message sent successfully\n");
        // Wait a bit for device to process initialization
        snooze(50000); // 50ms
    } else {
        printf("   ❌ Failed to send Introduction Message\n");
    }

    return result;
}

APCMiniError USBRawMIDI::SendNoteOn(uint8_t note, uint8_t velocity)
{
    return SendMIDI(MIDI_NOTE_ON | APC_MINI_MIDI_CHANNEL, note, velocity);
}

APCMiniError USBRawMIDI::SendNoteOff(uint8_t note)
{
    return SendMIDI(MIDI_NOTE_OFF | APC_MINI_MIDI_CHANNEL, note, 0);
}

APCMiniError USBRawMIDI::SendControlChange(uint8_t controller, uint8_t value)
{
    return SendMIDI(MIDI_CONTROL_CHANGE | APC_MINI_MIDI_CHANNEL, controller, value);
}

APCMiniError USBRawMIDI::SetPadColor(uint8_t pad, APCMiniLEDColor color)
{
    if (pad >= APC_MINI_PAD_COUNT) {
        return APC_ERROR_INVALID_PARAMETER;
    }

    uint8_t note = APC_MINI_PAD_NOTE_START + pad;
    uint8_t velocity = static_cast<uint8_t>(color);

    return SendNoteOn(note, velocity);
}

void USBRawMIDI::ResetStats()
{
    memset(&stats, 0, sizeof(stats));
    stats.min_latency_us = UINT32_MAX;
}

bool USBRawMIDI::FindAPCMini(char* device_path, size_t path_size)
{
    // This is handled by the USB roster now
    if (g_usb_roster && g_usb_roster->found_device) {
        strncpy(device_path, g_usb_roster->found_device->Location(), path_size - 1);
        device_path[path_size - 1] = '\0';
        return true;
    }
    return false;
}

int32 USBRawMIDI::ReaderThreadEntry(void* data)
{
    USBRawMIDI* midi = static_cast<USBRawMIDI*>(data);
    midi->ReaderThreadLoop();
    return 0;
}

void USBRawMIDI::ReaderThreadLoop()
{
    printf("   🔄 USB MIDI reader thread started (ultra-low latency mode)\n");

    while (!should_stop) {
        // Check if pause is requested
        if (pause_requested) {
            // Signal that we're paused
            is_paused = true;
            release_sem(pause_sem);

            // Wait until pause is lifted
            while (pause_requested && !should_stop) {
                snooze(1000); // 1ms
            }

            // Clear pause state
            is_paused = false;
            continue;
        }

        if (!g_usb_roster || !g_usb_roster->found_device) {
            snooze(1000); // 1ms - ultra low latency
            continue;
        }

        BUSBEndpoint* endpoint = g_usb_roster->endpoint_in;
        if (!endpoint) {
            snooze(1000); // 1ms - ultra low latency
            continue;
        }

        // Read USB MIDI packet - protected by endpoint lock
        usb_midi_event_packet packet;
        ssize_t result;

        {
            BAutolock auto_lock(endpoint_lock);
            if (!auto_lock.IsLocked()) {
                snooze(1000);
                continue;
            }

            // Use appropriate transfer method based on endpoint type
            if (endpoint->IsInterrupt()) {
                result = endpoint->InterruptTransfer(&packet, sizeof(packet));
            } else {
                result = endpoint->BulkTransfer(&packet, sizeof(packet));
            }
        }

        if (result >= 4) {
            // Process received MIDI packet
            if (packet.cin != 0 && midi_callback) {
                bigtime_t current_time = system_time();

                // Extract MIDI bytes from packet
                uint8_t status = packet.midi[0];
                uint8_t data1 = packet.midi[1];
                uint8_t data2 = packet.midi[2];

                // Call callback with MIDI data
                midi_callback(status, data1, data2);

                stats.messages_received++;
                last_message_time = current_time;
            }
        } else if (result < 0) {
            // Real error (negative return indicates error)
            printf("USB MIDI read error: %s\n", strerror(-result));
            stats.error_count++;
            snooze(5000); // 5ms pause on error - reduced for lower latency
        }

        // Minimal delay to prevent excessive CPU usage (ultra low latency mode)
        snooze(100); // 0.1ms - near real-time
    }

    printf("USB MIDI reader thread stopped\n");
}

void USBRawMIDI::PauseReader()
{
    if (reader_thread < 0 || pause_sem < 0) {
        return;
    }

    printf("[DEBUG] PauseReader: Requesting pause...\n");

    // Request pause
    pause_requested = true;

    // Wait for reader thread to acknowledge pause (with timeout)
    status_t result = acquire_sem_etc(pause_sem, 1, B_RELATIVE_TIMEOUT, 100000); // 100ms timeout
    if (result == B_OK) {
        printf("[DEBUG] PauseReader: Reader thread paused successfully\n");
    } else {
        printf("[DEBUG] PauseReader: Timeout waiting for pause (thread may be blocked)\n");
    }
}

void USBRawMIDI::ResumeReader()
{
    if (reader_thread < 0) {
        return;
    }

    printf("[DEBUG] ResumeReader: Resuming reader thread...\n");

    // Clear pause request
    pause_requested = false;

    printf("[DEBUG] ResumeReader: Reader thread resumed\n");
}

// USB Device Scanner implementation (simplified for Haiku)
int USBDeviceScanner::ScanUSBDevices(USBDevice* /*devices*/, int /*max_devices*/)
{
    // Use listusb output or direct USB Kit enumeration
    // For now, simplified implementation
    return 0;
}

bool USBDeviceScanner::IsAPCMini(const USBDevice& device)
{
    return (device.vendor_id == APC_MINI_VENDOR_ID &&
            (device.product_id == APC_MINI_PRODUCT_ID ||
             device.product_id == APC_MINI_MK2_PRODUCT_ID));
}

void USBDeviceScanner::PrintDeviceInfo(const USBDevice& device)
{
    printf("USB Device: %04X:%04X %s %s\n",
           device.vendor_id, device.product_id,
           device.manufacturer, device.product);
}