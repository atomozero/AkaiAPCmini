#include "usb_raw_midi.h"
#include <stdio.h>
#include <string.h>
#include <OS.h>

// Stub implementation for USB Raw MIDI when API is not available

USBRawMIDI::USBRawMIDI()
    : device_fd(-1)
    , interface_num(-1)
    , endpoint_in(-1)
    , endpoint_out(-1)
    , reader_thread(-1)
    , should_stop(false)
    , last_message_time(0)
{
    memset(&stats, 0, sizeof(stats));
    stats.min_latency_us = UINT32_MAX;
}

USBRawMIDI::~USBRawMIDI()
{
    Shutdown();
}

APCMiniError USBRawMIDI::Initialize()
{
    printf("USB Raw API not available in this Haiku build - falling back to MIDI API\n");
    return APC_ERROR_USB_OPEN_FAILED;
}

void USBRawMIDI::Shutdown()
{
    // Stub - nothing to shutdown
}

APCMiniError USBRawMIDI::SendMIDI(uint8_t /*status*/, uint8_t /*data1*/, uint8_t /*data2*/)
{
    // Stub - not implemented
    return APC_ERROR_USB_TRANSFER_FAILED;
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
    return SendNoteOn(pad, static_cast<uint8_t>(color));
}

void USBRawMIDI::ResetStats()
{
    memset(&stats, 0, sizeof(stats));
    stats.min_latency_us = UINT32_MAX;
}

bool USBRawMIDI::FindAPCMini(char* /*device_path*/, size_t /*path_size*/)
{
    // Stub - device not found
    return false;
}

APCMiniError USBRawMIDI::OpenDevice(const char* /*device_path*/)
{
    return APC_ERROR_USB_OPEN_FAILED;
}

void USBRawMIDI::CloseDevice()
{
    // Stub
}

APCMiniError USBRawMIDI::ClaimInterface()
{
    return APC_ERROR_USB_CLAIM_FAILED;
}

APCMiniError USBRawMIDI::FindEndpoints()
{
    return APC_ERROR_USB_OPEN_FAILED;
}

APCMiniError USBRawMIDI::SendUSBMIDIPacket(const USBMIDIEventPacket& /*packet*/)
{
    return APC_ERROR_USB_TRANSFER_FAILED;
}

void USBRawMIDI::ProcessUSBMIDIPacket(const USBMIDIEventPacket& /*packet*/)
{
    // Stub
}

int32 USBRawMIDI::ReaderThreadEntry(void* /*data*/)
{
    return 0;
}

void USBRawMIDI::ReaderThreadLoop()
{
    // Stub
}

void USBRawMIDI::UpdateLatencyStats(bigtime_t /*latency*/)
{
    // Stub
}

uint8_t USBRawMIDI::CalculateUSBMIDIHeader(uint8_t /*cable*/, uint8_t /*status*/)
{
    return 0;
}

// USBDeviceScanner stubs
int USBDeviceScanner::ScanUSBDevices(USBDevice* /*devices*/, int /*max_devices*/)
{
    printf("USB device scanning not available\n");
    return 0;
}

bool USBDeviceScanner::IsAPCMini(const USBDevice& /*device*/)
{
    return false;
}

void USBDeviceScanner::PrintDeviceInfo(const USBDevice& device)
{
    printf("Device: %s (VID: 0x%04x, PID: 0x%04x)\n",
           device.path, device.vendor_id, device.product_id);
}