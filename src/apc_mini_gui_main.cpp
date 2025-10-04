#include "apc_mini_gui.h"
#include "midi_message_queue.h"
#include "midi_event_handler.h"
#include <stdio.h>
#include <signal.h>

// ===============================
// MIDI Endpoint Classes Implementation
// ===============================

APCMiniMIDIConsumer::APCMiniMIDIConsumer(APCMiniGUIApp* app) : gui_app(app) {
    SetName("APC Mini GUI Input");

    // Set properties using BMessage
    BMessage* properties = new BMessage();
    properties->AddString("be:manufacturer", "AKAI Professional");
    properties->AddString("be:product", "APC mini mk2 GUI Controller");
    properties->AddString("be:version", "1.0.0");
    properties->AddString("be:description", "Virtual APC Mini MK2 Controller GUI - receives MIDI input for visual feedback");
    properties->AddString("be:type", "controller");
    properties->AddString("be:category", "Hardware Controller");
    properties->AddString("be:device_type", "Virtual MIDI Controller");
    properties->AddString("be:channels", "1-16");
    properties->AddString("be:notes", "Note 0-63: RGB pads | Note 100-107: Track buttons | Note 112-119: Scene buttons | Note 122: Shift");
    properties->AddString("be:controllers", "CC 48-55: Track faders | CC 56: Master fader");
    properties->AddString("be:features", "Real-time visual feedback, bidirectional hardware sync, RGB LED simulation");
    SetProperties(properties);
    delete properties;
}

void APCMiniMIDIConsumer::NoteOn(uchar /*channel*/, uchar note, uchar velocity, bigtime_t /*time*/) {
    if (gui_app) {
        gui_app->HandleNoteOn(note, velocity);
    }
}

void APCMiniMIDIConsumer::NoteOff(uchar /*channel*/, uchar note, uchar velocity, bigtime_t /*time*/) {
    if (gui_app) {
        gui_app->HandleNoteOff(note, velocity);
    }
}

void APCMiniMIDIConsumer::ControlChange(uchar /*channel*/, uchar controller, uchar value, bigtime_t /*time*/) {
    if (gui_app) {
        gui_app->HandleControlChange(controller, value);
    }
}

APCMiniMIDIProducer::APCMiniMIDIProducer() {
    SetName("APC Mini GUI Output");

    // Set properties using BMessage
    BMessage* properties = new BMessage();
    properties->AddString("be:manufacturer", "AKAI Professional");
    properties->AddString("be:product", "APC mini mk2 GUI Output");
    properties->AddString("be:version", "1.0.0");
    properties->AddString("be:description", "Virtual APC Mini MK2 Controller GUI - sends MIDI output for LED control and feedback");
    properties->AddString("be:type", "controller");
    properties->AddString("be:category", "Hardware Controller");
    properties->AddString("be:device_type", "Virtual MIDI Controller");
    properties->AddString("be:channels", "1-16");
    properties->AddString("be:notes", "RGB LED control (Note On/Off Ch 1-6) | Button LEDs (Note On/Off)");
    properties->AddString("be:controllers", "LED brightness control, SysEx configuration messages");
    properties->AddString("be:features", "RGB LED feedback, button LED control, hardware synchronization");
    SetProperties(properties);
    delete properties;
}

// ===============================
// APCMiniGUIApp Implementation
// ===============================

const char* APC_GUI_APP_SIGNATURE = "application/x-vnd.akai-apc-mini-gui";

APCMiniGUIApp::APCMiniGUIApp()
    : BApplication(APC_GUI_APP_SIGNATURE)
    , main_window(nullptr)
    , usb_midi(nullptr)
    , sync_thread(-1)
    , should_stop(false)
    , use_hardware(true)
    , midi_queue(nullptr)
    , midi_handler(nullptr)
    , midi_looper(nullptr)
    , midi_consumer(nullptr)
    , midi_producer(nullptr)
{
    InitializeDeviceState();

    // Initialize new MIDI system
    midi_queue = new MIDIMessageQueue();
    midi_handler = new MIDIEventHandler("APC Mini MIDI Handler");
    midi_handler->SetMessageQueue(midi_queue);

    // Initialize Patchbay integration
    midi_consumer = new APCMiniMIDIConsumer(this);
    midi_producer = new APCMiniMIDIProducer();

    // Create and start MIDI event looper
    midi_looper = new MIDIEventLooper(midi_handler, "APC Mini MIDI Looper");
    midi_looper->Run();
    midi_looper->StartProcessing();

    // Register callbacks for different types of MIDI events
    RegisterMIDICallbacks();
}

APCMiniGUIApp::~APCMiniGUIApp()
{
    ShutdownHardware();

    // Shutdown MIDI system
    if (midi_looper) {
        midi_looper->StopProcessing();
        midi_looper->PostMessage(B_QUIT_REQUESTED);
        midi_looper = nullptr;
    }

    // Unregister and cleanup Patchbay endpoints
    if (midi_consumer) {
        midi_consumer->Unregister();
        delete midi_consumer;
        midi_consumer = nullptr;
    }

    if (midi_producer) {
        midi_producer->Unregister();
        delete midi_producer;
        midi_producer = nullptr;
    }

    delete midi_handler;
    midi_handler = nullptr;

    delete midi_queue;
    midi_queue = nullptr;
}

void APCMiniGUIApp::ReadyToRun()
{
    // Register MIDI endpoints with Patchbay
    printf("🎹 Initializing MIDI Patchbay Integration...\n");
    if (midi_consumer->Register() == B_OK) {
        printf("   ✓ MIDI Input registered  (ID: %d)\n", midi_consumer->ID());
    } else {
        printf("   ✗ Failed to register MIDI input\n");
    }

    if (midi_producer->Register() == B_OK) {
        printf("   ✓ MIDI Output registered (ID: %d)\n", midi_producer->ID());
    } else {
        printf("   ✗ Failed to register MIDI output\n");
    }

    // List available endpoints for user information
    printf("\n📡 MIDI Endpoints Ready:\n");
    printf("   📥 Input:  '%s'\n", midi_consumer->Name());
    printf("      └─ Connect APC Mini hardware or other MIDI controllers\n");
    printf("   📤 Output: '%s'\n", midi_producer->Name());
    printf("      └─ Connect to synthesizers, DAW software, or other MIDI devices\n");
    printf("   💡 Tip: Use the Patchbay application to create MIDI connections\n\n");

    // Create main window
    main_window = new APCMiniWindow();
    main_window->app = this;
    main_window->Show();

    // Try to initialize hardware
    if (InitializeHardware()) {
        main_window->SetConnectionStatus(true);
        printf("\n🎉 APC Mini device ready! Hardware connection established.\n");

        // Query current fader positions to synchronize GUI with hardware
        // Note: Removed snooze delay to improve startup responsiveness
        QueryFaderPositions();

        printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    } else {
        main_window->SetConnectionStatus(false);
        printf("\n⚠️  No APC Mini device found - running in GUI simulation mode.\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
        use_hardware = false;
    }
}

bool APCMiniGUIApp::QuitRequested()
{
    should_stop = true;

    // Wait for sync thread to finish
    if (sync_thread >= 0) {
        status_t exit_value;
        wait_for_thread(sync_thread, &exit_value);
        sync_thread = -1;
    }

    return true;
}

void APCMiniGUIApp::MessageReceived(BMessage* message)
{
    BApplication::MessageReceived(message);
}

bool APCMiniGUIApp::InitializeHardware()
{
    if (usb_midi) {
        return true; // Already initialized
    }

    usb_midi = new USBRawMIDI();

    // Set up MIDI callback to use new queue system
    usb_midi->SetMIDICallback([this](uint8_t status, uint8_t data1, uint8_t data2) {
        // Log incoming MIDI message
        if (main_window && main_window->debug_window) {
            main_window->debug_window->LogMIDIMessage("RX", status, data1, data2);
        }

        // Submit to queue for thread-safe processing
        if (midi_handler) {
            midi_handler->SubmitEvent(status, data1, data2, MIDI_SOURCE_HARDWARE_USB);
        } else {
            // Fallback to message posting for thread-safe GUI updates
            if (main_window) {
                BMessage msg(MSG_HARDWARE_MIDI_EVENT);
                msg.AddUInt8("status", status);
                msg.AddUInt8("data1", data1);
                msg.AddUInt8("data2", data2);
                main_window->PostMessage(&msg);
            }
        }
    });

    APCMiniError result = usb_midi->Initialize();
    if (result != APC_SUCCESS) {
        delete usb_midi;
        usb_midi = nullptr;
        return false;
    }

    // Start synchronization thread
    sync_thread = spawn_thread(SyncThreadEntry, "apc_sync", B_NORMAL_PRIORITY, this);
    if (sync_thread >= 0) {
        resume_thread(sync_thread);
    }

    return true;
}

void APCMiniGUIApp::ShutdownHardware()
{
    should_stop = true;

    if (sync_thread >= 0) {
        status_t exit_value;
        wait_for_thread(sync_thread, &exit_value);
        sync_thread = -1;
    }

    if (usb_midi) {
        usb_midi->Shutdown();
        delete usb_midi;
        usb_midi = nullptr;
    }
}

bool APCMiniGUIApp::IsHardwareConnected() const
{
    return usb_midi && usb_midi->IsConnected();
}

void APCMiniGUIApp::SendNoteOn(uint8_t note, uint8_t velocity)
{
    // Send via USB Raw (direct hardware)
    if (usb_midi && usb_midi->IsConnected()) {
        // Log outgoing MIDI message
        if (main_window && main_window->debug_window) {
            main_window->debug_window->LogMIDIMessage("TX", MIDI_NOTE_ON | APC_MINI_MIDI_CHANNEL, note, velocity);
        }
        usb_midi->SendNoteOn(note, velocity);
    }

    // Also send via Patchbay (for external connections)
    if (midi_producer) {
        midi_producer->SprayNoteOn(APC_MINI_MIDI_CHANNEL, note, velocity, system_time());
    }

    // Update device state
    if (IS_PAD_NOTE(note)) {
        uint8_t pad_index = note - APC_MINI_PAD_NOTE_START;
        if (pad_index < APC_MINI_PAD_COUNT) {
            device_state.pads[pad_index] = true;
            device_state.pad_velocities[pad_index] = velocity;
        }
    } else if (IS_TRACK_NOTE(note)) {
        uint8_t button_index = note - APC_MINI_TRACK_NOTE_START;
        if (button_index < 8) {
            device_state.track_buttons[button_index] = true;
        }
    } else if (IS_SCENE_NOTE(note)) {
        uint8_t button_index = note - APC_MINI_SCENE_NOTE_START;
        if (button_index < 8) {
            device_state.scene_buttons[button_index] = true;
        }
    } else if (IS_SHIFT_NOTE(note)) {
        device_state.shift_pressed = true;
    }
}

void APCMiniGUIApp::SendNoteOff(uint8_t note)
{
    // Send via USB Raw (direct hardware)
    if (usb_midi && usb_midi->IsConnected()) {
        // Log outgoing MIDI message
        if (main_window && main_window->debug_window) {
            main_window->debug_window->LogMIDIMessage("TX", MIDI_NOTE_OFF | APC_MINI_MIDI_CHANNEL, note, 0);
        }
        usb_midi->SendNoteOff(note);
    }

    // Also send via Patchbay (for external connections)
    if (midi_producer) {
        midi_producer->SprayNoteOff(APC_MINI_MIDI_CHANNEL, note, 0, system_time());
    }

    // Update device state
    if (IS_PAD_NOTE(note)) {
        uint8_t pad_index = note - APC_MINI_PAD_NOTE_START;
        if (pad_index < APC_MINI_PAD_COUNT) {
            device_state.pads[pad_index] = false;
            device_state.pad_velocities[pad_index] = 0;
        }
    } else if (IS_TRACK_NOTE(note)) {
        uint8_t button_index = note - APC_MINI_TRACK_NOTE_START;
        if (button_index < 8) {
            device_state.track_buttons[button_index] = false;
        }
    } else if (IS_SCENE_NOTE(note)) {
        uint8_t button_index = note - APC_MINI_SCENE_NOTE_START;
        if (button_index < 8) {
            device_state.scene_buttons[button_index] = false;
        }
    } else if (IS_SHIFT_NOTE(note)) {
        device_state.shift_pressed = false;
    }
}

void APCMiniGUIApp::SendControlChange(uint8_t controller, uint8_t value)
{
    // Send via USB Raw (direct hardware)
    if (usb_midi && usb_midi->IsConnected()) {
        // Log outgoing MIDI message
        if (main_window && main_window->debug_window) {
            main_window->debug_window->LogMIDIMessage("TX", MIDI_CONTROL_CHANGE | APC_MINI_MIDI_CHANNEL, controller, value);
        }
        usb_midi->SendControlChange(controller, value);
    }

    // Also send via Patchbay (for external connections)
    if (midi_producer) {
        midi_producer->SprayControlChange(APC_MINI_MIDI_CHANNEL, controller, value, system_time());
    }

    // Update device state
    if (IS_TRACK_FADER_CC(controller)) {
        uint8_t fader_index = controller - APC_MINI_FADER_CC_START;
        if (fader_index < APC_MINI_TRACK_FADER_COUNT) {
            device_state.track_fader_values[fader_index] = value;
        }
    } else if (IS_MASTER_FADER_CC(controller)) {
        device_state.master_fader_value = value;
    }
}

void APCMiniGUIApp::SendPadRGB(uint8_t pad_index, const APCMiniMK2RGB& color)
{
    if (!usb_midi || !usb_midi->IsConnected()) {
        return;
    }

    if (pad_index >= APC_MINI_PAD_COUNT) {
        return;
    }

    // Update device state
    device_state.pad_rgb_colors[pad_index] = color;

    // Use Note On with velocity for basic color control
    uint8_t note = APC_MINI_PAD_NOTE_START + pad_index;

    // Convert RGB to APC Mini velocity values
    // APC Mini MK2 responds to Note On velocities for basic colors:
    // 0 = Off, 1 = Green, 3 = Red, 5 = Yellow, etc.
    uint8_t velocity = 0;

    if (color.red == 0 && color.green == 0 && color.blue == 0) {
        velocity = 0; // Off
    } else if (color.red > 200 && color.green > 100 && color.blue == 0) {
        velocity = 5; // Orange/Yellow (like Haiku logo)
    } else if (color.red > 100 && color.green == 0 && color.blue == 0) {
        velocity = 3; // Red
    } else if (color.red == 0 && color.green > 100 && color.blue == 0) {
        velocity = 1; // Green
    } else if (color.red == 0 && color.green == 0 && color.blue > 100) {
        velocity = 79; // Blue (higher velocity)
    } else if (color.red > 100 && color.green > 100 && color.blue == 0) {
        velocity = 5; // Yellow
    } else if (color.red > 100 && color.green == 0 && color.blue > 100) {
        velocity = 53; // Magenta
    } else if (color.red == 0 && color.green > 100 && color.blue > 100) {
        velocity = 37; // Cyan
    } else {
        velocity = 127; // White/bright
    }

    // Send Note On command
    usb_midi->SendNoteOn(note, velocity);
}

void APCMiniGUIApp::SetTrackButtonLED(uint8_t button_index, bool on)
{
    if (button_index >= 8) return;

    if (usb_midi && usb_midi->IsConnected()) {
        uint8_t note = APC_MINI_TRACK_NOTE_START + button_index;
        if (on) {
            usb_midi->SendNoteOn(note, 127);
        } else {
            usb_midi->SendNoteOff(note);
        }
    }

    device_state.track_buttons[button_index] = on;
}

void APCMiniGUIApp::SetSceneButtonLED(uint8_t button_index, bool on)
{
    if (button_index >= 8) return;

    if (usb_midi && usb_midi->IsConnected()) {
        uint8_t note = APC_MINI_SCENE_NOTE_START + button_index;
        if (on) {
            usb_midi->SendNoteOn(note, 127);
        } else {
            usb_midi->SendNoteOff(note);
        }
    }

    device_state.scene_buttons[button_index] = on;
}

void APCMiniGUIApp::ResetDeviceState()
{
    // Reset all state
    memset(&device_state, 0, sizeof(device_state));
    device_state.is_mk2_device = true; // Assume MK2 for GUI

    // Send reset commands to hardware
    if (usb_midi && usb_midi->IsConnected()) {
        // Turn off all LEDs
        for (int i = 0; i < APC_MINI_PAD_COUNT; i++) {
            usb_midi->SendNoteOff(APC_MINI_PAD_NOTE_START + i);
        }

        for (int i = 0; i < 8; i++) {
            usb_midi->SendNoteOff(APC_MINI_TRACK_NOTE_START + i);
            usb_midi->SendNoteOff(APC_MINI_SCENE_NOTE_START + i);
        }

        // Reset all faders to 0
        for (int i = 0; i < APC_MINI_TRACK_FADER_COUNT; i++) {
            usb_midi->SendControlChange(APC_MINI_FADER_CC_START + i, 0);
            device_state.track_fader_values[i] = 0; // Update state to match hardware
        }
        usb_midi->SendControlChange(APC_MINI_MASTER_CC, 0);
        device_state.master_fader_value = 0; // Update state to match hardware
    }

    // Update GUI to match reset state
    if (main_window) {
        main_window->UpdateFromDevice(device_state);
    }
}

void APCMiniGUIApp::HandleMIDIMessage(uint8_t status, uint8_t data1, uint8_t data2)
{
    uint8_t msg_type = status & 0xF0;
    // uint8_t channel = status & 0x0F;  // Not used currently

    switch (msg_type) {
        case MIDI_NOTE_ON:
            if (data2 > 0) {
                HandleNoteOn(data1, data2);
            } else {
                HandleNoteOff(data1, data2);
            }
            break;

        case MIDI_NOTE_OFF:
            HandleNoteOff(data1, data2);
            break;

        case MIDI_CONTROL_CHANGE:
            HandleControlChange(data1, data2);
            break;
    }
}

void APCMiniGUIApp::HandleNoteOn(uint8_t note, uint8_t velocity)
{
    if (IS_PAD_NOTE(note)) {
        uint8_t pad_index = note - APC_MINI_PAD_NOTE_START;
        if (main_window) {
            main_window->HandlePadPress(pad_index, velocity);
        }
    } else if (IS_TRACK_NOTE(note)) {
        uint8_t button_index = note - APC_MINI_TRACK_NOTE_START;
        if (main_window) {
            main_window->HandleTrackButton(button_index, true);
        }
    } else if (IS_SCENE_NOTE(note)) {
        uint8_t button_index = note - APC_MINI_SCENE_NOTE_START;
        if (main_window) {
            main_window->HandleSceneButton(button_index, true);
        }
    } else if (IS_SHIFT_NOTE(note)) {
        if (main_window) {
            main_window->HandleShiftButton(true);
        }
    }
}

void APCMiniGUIApp::HandleNoteOff(uint8_t note, uint8_t /*velocity*/)
{
    if (IS_PAD_NOTE(note)) {
        uint8_t pad_index = note - APC_MINI_PAD_NOTE_START;
        if (main_window) {
            main_window->HandlePadRelease(pad_index);
        }
    } else if (IS_TRACK_NOTE(note)) {
        uint8_t button_index = note - APC_MINI_TRACK_NOTE_START;
        if (main_window) {
            main_window->HandleTrackButton(button_index, false);
        }
    } else if (IS_SCENE_NOTE(note)) {
        uint8_t button_index = note - APC_MINI_SCENE_NOTE_START;
        if (main_window) {
            main_window->HandleSceneButton(button_index, false);
        }
    } else if (IS_SHIFT_NOTE(note)) {
        if (main_window) {
            main_window->HandleShiftButton(false);
        }
    }
}

void APCMiniGUIApp::HandleControlChange(uint8_t controller, uint8_t value)
{
    if (IS_TRACK_FADER_CC(controller)) {
        uint8_t fader_index = controller - APC_MINI_FADER_CC_START;

        // Update device state to stay in sync with hardware
        device_state.track_fader_values[fader_index] = value;

        if (main_window) {
            main_window->HandleFaderChange(fader_index, value);
        }
    } else if (IS_MASTER_FADER_CC(controller)) {
        // Update device state to stay in sync with hardware
        device_state.master_fader_value = value;

        if (main_window) {
            main_window->HandleFaderChange(APC_MINI_TRACK_FADER_COUNT, value);
        }
    }
}

int32 APCMiniGUIApp::SyncThreadEntry(void* data)
{
    APCMiniGUIApp* app = static_cast<APCMiniGUIApp*>(data);
    app->SyncThreadLoop();
    return 0;
}

void APCMiniGUIApp::SyncThreadLoop()
{
    while (!should_stop) {
        if (usb_midi && usb_midi->IsConnected() && main_window) {
            // NOTE: Removed UpdateGUIFromState() call here as it was overwriting
            // fader values with stale device_state data every 50ms, causing
            // fader synchronization bugs. GUI updates are handled directly
            // when hardware changes are received via HandleControlChange().
        }

        snooze(50000); // 50ms update interval
    }
}

void APCMiniGUIApp::UpdateGUIFromState()
{
    if (main_window && main_window->Lock()) {
        main_window->UpdateFromDevice(device_state);
        main_window->Unlock();
    }
}

void APCMiniGUIApp::InitializeDeviceState()
{
    memset(&device_state, 0, sizeof(device_state));
    device_state.is_mk2_device = true; // Default to MK2 for GUI purposes
    device_state.led_mode = APC_MK2_LED_MODE_RGB;
    device_state.device_mode = APC_MK2_MODE_SESSION;
}

void APCMiniGUIApp::QueryFaderPositions()
{
    printf("   🔍 Initializing fader synchronization system...\n");

    // HARDWARE REALITY: APC Mini faders are INPUT-ONLY controls
    // - They cannot be moved remotely via MIDI commands
    // - They don't report current position automatically
    // - Position is only sent when physically moved

    // SOLUTION: Implement visual sync indicator system
    if (main_window && main_window->Lock()) {
        // Set all faders to a special "unknown" state
        // Use a distinct value that clearly indicates "need sync"
        const uint8_t unknown_indicator = 1; // Very bottom to show "needs sync"

        for (int i = 0; i < APC_MINI_TRACK_FADER_COUNT; i++) {
            main_window->fader_panel->SetFaderValue(i, unknown_indicator);
            device_state.track_fader_values[i] = unknown_indicator;
        }

        // Set master fader to unknown state too
        main_window->fader_panel->SetFaderValue(APC_MINI_TRACK_FADER_COUNT, unknown_indicator);
        device_state.master_fader_value = unknown_indicator;

        main_window->Unlock();
    }

    printf("   ✓ Fader sync system ready\n");
    printf("   📍 GUI faders show bottom position (sync required)\n");
    printf("   🎛️  INSTRUCTIONS FOR SYNC:\n");
    printf("       1. Move each physical fader you want to use\n");
    printf("       2. GUI will immediately sync to real position\n");
    printf("       3. Once moved, fader stays perfectly synchronized\n");
    printf("   💡 Tip: You only need to sync faders you plan to use\n");
}

uint8_t APCMiniGUIApp::ScanSingleFader(uint8_t /*cc_number*/)
{
    // This method is now unused since we can't actually scan APC Mini faders
    // Hardware limitation: No position feedback capability
    return 1; // Return "unknown" indicator value
}

rgb_color APCMiniGUIApp::MIDIVelocityToRGB(uint8_t velocity)
{
    // Simple velocity to color mapping
    if (velocity == 0) {
        return APC_GUI_PAD_OFF_COLOR;
    }

    uint8_t intensity = (velocity * 255) / 127;
    return rgb_color{intensity, static_cast<uint8_t>(intensity / 2), static_cast<uint8_t>(intensity / 4), 255};
}

APCMiniMK2RGB APCMiniGUIApp::VelocityToMK2RGB(uint8_t velocity)
{
    if (velocity == 0) {
        return {0, 0, 0};
    }

    // Map velocity to a colorful RGB value
    uint8_t r = velocity;
    uint8_t g = (velocity * 3) / 4;
    uint8_t b = velocity / 2;

    return {r, g, b};
}

void APCMiniGUIApp::RegisterMIDICallbacks()
{
    if (!midi_handler) return;

    // Register callback for pad presses (Note On/Off events)
    MIDIEventFilter pad_filter;
    pad_filter.accept_note_on = true;
    pad_filter.accept_note_off = true;
    pad_filter.accept_cc = false;
    pad_filter.accept_sysex = false;

    midi_handler->RegisterCallback([this](const MIDIMessage& msg) {
        // Post message to main thread for thread-safe GUI updates
        if (main_window) {
            BMessage bmsg(MSG_HARDWARE_MIDI_EVENT);
            bmsg.AddUInt8("status", msg.status);
            bmsg.AddUInt8("data1", msg.data1);
            bmsg.AddUInt8("data2", msg.data2);
            main_window->PostMessage(&bmsg);
        }
    }, pad_filter);

    // Register callback for fader movements (Control Change events)
    MIDIEventFilter fader_filter;
    fader_filter.accept_note_on = false;
    fader_filter.accept_note_off = false;
    fader_filter.accept_cc = true;
    fader_filter.accept_sysex = false;

    midi_handler->RegisterCallback([this](const MIDIMessage& msg) {
        // Post message to main thread for thread-safe GUI updates
        if (main_window) {
            BMessage bmsg(MSG_HARDWARE_MIDI_EVENT);
            bmsg.AddUInt8("status", msg.status);
            bmsg.AddUInt8("data1", msg.data1);
            bmsg.AddUInt8("data2", msg.data2);
            main_window->PostMessage(&bmsg);
        }
    }, fader_filter);

    // Register callback for SysEx messages (APC Mini MK2 RGB data)
    MIDIEventFilter sysex_filter;
    sysex_filter.accept_note_on = false;
    sysex_filter.accept_note_off = false;
    sysex_filter.accept_cc = false;
    sysex_filter.accept_sysex = true;

    midi_handler->RegisterCallback([this](const MIDIMessage& msg) {
        // Post message to main thread for thread-safe GUI updates
        if (main_window) {
            BMessage bmsg(MSG_HARDWARE_MIDI_EVENT);
            bmsg.AddUInt8("status", msg.status);
            bmsg.AddUInt8("data1", msg.data1);
            bmsg.AddUInt8("data2", msg.data2);
            main_window->PostMessage(&bmsg);
        }
    }, sysex_filter);

    // Set event priorities for real-time performance
    midi_handler->SetEventPriority(0x90, MIDI_PRIORITY_HIGH);      // Note On
    midi_handler->SetEventPriority(0x80, MIDI_PRIORITY_HIGH);      // Note Off
    midi_handler->SetEventPriority(0xB0, MIDI_PRIORITY_NORMAL);    // Control Change
    midi_handler->SetEventPriority(0xF0, MIDI_PRIORITY_LOW);       // SysEx

    // Enable feedback prevention
    midi_handler->SetFeedbackPrevention(true);
}

// ===============================
// Main Function
// ===============================

int main(int argc, char* argv[])
{
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│             APC Mini MK2 GUI Controller v1.0               │\n");
    printf("│                     for Haiku OS                           │\n");
    printf("└─────────────────────────────────────────────────────────────┘\n\n");

    // Handle command line arguments
    bool simulation_mode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sim") == 0 || strcmp(argv[i], "--simulation") == 0) {
            simulation_mode = true;
            printf("Running in simulation mode (no hardware required)\n");
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --sim, --simulation    Run in simulation mode (no hardware)\n");
            printf("  --help, -h            Show this help message\n");
            return 0;
        }
    }

    APCMiniGUIApp app;

    if (simulation_mode) {
        printf("Hardware connection disabled.\n");
    }

    app.Run();

    printf("APC Mini GUI shut down.\n");
    return 0;
}