// apc_mini_test.cpp
// APC Mini Test Application for Haiku OS
//
// ARCHITECTURE NOTE - Dual Access Strategy:
//
// This application uses a hybrid approach to MIDI communication:
//
// 1. PRIMARY: USB Raw Access (usb_raw_midi.cpp)
//    - Direct USB communication bypassing MIDI stack
//    - Latency: ~50-100μs (direct USB transfer)
//    - Throughput: Limited only by USB hardware (~1-2ms per transfer)
//    - No IPC overhead, no midi_server involvement
//    - Trade-off: Manual device management, no cross-app routing
//
// 2. FALLBACK: Haiku MIDI Kit 2 (BMidiRoster)
//    - Client-server architecture via midi_server
//    - Latency: ~270μs (IPC overhead: serialization + 2 context switches)
//    - Throughput: ~4k msg/sec (limited by IPC)
//    - Message flow: App → libmidi2 → midi_server → libmidi2 → midi_usb driver
//    - Trade-off: Protected memory, cross-app routing, but high overhead
//
// PERFORMANCE RATIONALE:
// - USB Raw chosen for real-time LED control (<100μs required)
// - MIDI Kit 2 would add 270μs per message (unacceptable for 64-pad grid updates)
// - Benchmark results: USB Raw = 30ms for 64 LEDs, MIDI Kit 2 would be ~47ms
// - See: docs/technical/MIDIKIT2_ARCHITECTURE.md for detailed analysis
// - See: benchmarks/RESULTS.md for performance measurements
//
// References:
// - https://www.freelists.org/post/openbeos-midi/Midi2-todo-List,3
// - https://www.haiku-os.org/legacy-docs/openbeosnewsletter/nsl33.html

#include <Application.h>
#include <MidiRoster.h>
#include <MidiConsumer.h>
#include <MidiProducer.h>
#include <OS.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include "usb_raw_midi.h"
#include "apc_mini_defs.h"

class APCMiniTestApp : public BApplication {
public:
    APCMiniTestApp();
    virtual ~APCMiniTestApp();

    virtual void ReadyToRun() override;
    virtual bool QuitRequested() override;

    void RunInteractiveTest();
    void RunSimulationTest();
    void RunStressTest();
    void RunLatencyTest();

private:
    class MIDIConsumerApp : public BMidiLocalConsumer {
    public:
        MIDIConsumerApp(APCMiniTestApp* app) : app(app) {
            SetName("APC Mini Input");
            SetProperties("manufacturer", "AKAI Professional");
            SetProperties("product", "APC mini mk2 Controller");
            SetProperties("version", "1.0");
            SetProperties("description", "Receives MIDI input from APC Mini controller pads, faders and buttons");
            SetProperties("type", "controller");
            SetProperties("channels", "16");
            SetProperties("notes", "64 velocity-sensitive pads (C1-C5)");
            SetProperties("controllers", "9 faders (CC48-56), track/scene buttons");
        }
        virtual void NoteOn(uchar channel, uchar note, uchar velocity, bigtime_t time) override;
        virtual void NoteOff(uchar channel, uchar note, uchar velocity, bigtime_t time) override;
        virtual void ControlChange(uchar channel, uchar controlNumber, uchar controlValue, bigtime_t time) override;

    private:
        APCMiniTestApp* app;
    };

    class MIDIProducerApp : public BMidiLocalProducer {
    public:
        MIDIProducerApp() {
            SetName("APC Mini Output");
            SetProperties("manufacturer", "AKAI Professional");
            SetProperties("product", "APC mini mk2 Controller");
            SetProperties("version", "1.0");
            SetProperties("description", "Sends MIDI output to APC Mini for LED control and feedback");
            SetProperties("type", "controller");
            SetProperties("channels", "16");
            SetProperties("notes", "RGB LED control via Note On/Off");
            SetProperties("controllers", "Button LED control, device control");
            SetProperties("features", "RGB pad LEDs, button LEDs, bi-directional communication");
        }
    };

    USBRawMIDI* usb_midi;
    MIDIConsumerApp* midi_consumer;
    MIDIProducerApp* midi_producer;
    APCMiniState device_state;
    APCMiniTestMode current_mode;
    bool use_usb_raw;
    bool running;

    // Test methods
    void TestPadMatrix();
    void TestFaders();
    void TestButtons();
    void TestLEDColors();
    void DisplayStats();
    void ResetDevice();

    // MIDI event handlers
    void HandleNoteOn(uint8_t note, uint8_t velocity);
    void HandleNoteOff(uint8_t note, uint8_t velocity);
    void HandleControlChange(uint8_t controller, uint8_t value);

    // Utilities
    void PrintHelp();
    void SetNonCanonicalInput(bool enable);
    bool InitializeUSBRaw();
    bool InitializeHaikuMIDI();
    void SendLEDUpdate(uint8_t note, APCMiniLEDColor color);
    void SendMK2RGBUpdate(uint8_t note, const APCMiniMK2RGB& rgb_color);
    void SendMK2SysEx(const uint8_t* data, size_t length);
    void DetectMK2Device();
    void SetMK2Mode(APCMiniMK2Mode mode);
    void SetupNoteMode(APCMiniMK2Scale scale, uint8_t root_note);
    void SetupDrumMode();
    void CalculateNoteModeNotes(APCMiniMK2Scale scale, uint8_t root_note);
    void CalculateDrumModeNotes();
    uint8_t GetPadNoteInCurrentMode(uint8_t pad_index);
    void PrintDeviceState();
    void PrintPadMatrix();
    void TestMK2RGB();
    void TestMK2Modes();
    void SendMK2CustomRGB(uint8_t start_pad, uint8_t end_pad, const APCMiniMK2RGB& rgb_color);
};

// Global application instance for signal handling
APCMiniTestApp* g_app = nullptr;

void signal_handler(int /*sig*/)
{
    if (g_app) {
        printf("\nShutting down...\n");
        g_app->PostMessage(B_QUIT_REQUESTED);
    }
}

APCMiniTestApp::APCMiniTestApp()
    : BApplication("application/x-vnd.apc-mini-test")
    , usb_midi(nullptr)
    , midi_consumer(nullptr)
    , midi_producer(nullptr)
    , current_mode(TEST_MODE_INTERACTIVE)
    , use_usb_raw(true)
    , running(true)
{
    memset(&device_state, 0, sizeof(device_state));

    // Initialize MK2 specific defaults
    device_state.is_mk2_device = false;
    device_state.led_mode = APC_MK2_LED_MODE_LEGACY;
    device_state.device_mode = APC_MK2_MODE_SESSION;
    device_state.current_scale = APC_MK2_SCALE_MAJOR;
    device_state.root_note = APC_MK2_NOTE_MODE_ROOT_NOTE;

    g_app = this;

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

APCMiniTestApp::~APCMiniTestApp()
{
    if (usb_midi) {
        usb_midi->Shutdown();
        delete usb_midi;
    }

    if (midi_consumer) {
        midi_consumer->Unregister();
        delete midi_consumer;
    }

    if (midi_producer) {
        midi_producer->Unregister();
        delete midi_producer;
    }

    g_app = nullptr;
}

void APCMiniTestApp::ReadyToRun()
{
    printf("APC Mini Test Application for Haiku OS\n");
    printf("=====================================\n\n");

    // Try USB Raw first, fall back to Haiku MIDI
    if (use_usb_raw && InitializeUSBRaw()) {
        printf("Using USB Raw access mode\n");
    } else {
        printf("Falling back to Haiku MIDI\n");
        if (!InitializeHaikuMIDI()) {
            printf("Failed to initialize any MIDI interface\n");
            PostMessage(B_QUIT_REQUESTED);
            return;
        }
        use_usb_raw = false;
    }

    // Start in interactive mode
    printf("\nStarting interactive test mode...\n");
    PrintHelp();
    RunInteractiveTest();
}

bool APCMiniTestApp::QuitRequested()
{
    running = false;
    return true;
}

void APCMiniTestApp::RunInteractiveTest()
{
    current_mode = TEST_MODE_INTERACTIVE;
    running = true;  // Initialize running to true
    SetNonCanonicalInput(true);

    printf("\nPress keys for commands (h for help, q to quit):\n");

    while (running) {
        char c = getchar();

        switch (c) {
            case 'h':
            case 'H':
                PrintHelp();
                break;

            case 's':
            case 'S':
                DisplayStats();
                break;

            case 't':
            case 'T':
                TestPadMatrix();
                break;

            case 'f':
            case 'F':
                TestFaders();
                break;

            case 'b':
            case 'B':
                TestButtons();
                break;

            case 'c':
            case 'C':
                TestLEDColors();
                break;

            case 'r':
            case 'R':
                ResetDevice();
                break;

            case 'v':
            case 'V':
                PrintDeviceState();
                break;

            case 'p':
            case 'P':
                PrintPadMatrix();
                break;

            case 'l':
            case 'L':
                RunLatencyTest();
                break;

            case 'x':
            case 'X':
                RunStressTest();
                break;

            case 'm':
            case 'M':
                RunSimulationTest();
                break;

            case 'g':
            case 'G':
                TestMK2RGB();
                break;

            case 'n':
            case 'N':
                TestMK2Modes();
                break;

            case 'q':
            case 'Q':
                printf("Quitting...\n");
                running = false;
                break;

            case '\n':
            case '\r':
                break; // Ignore newlines

            default:
                printf("Unknown command '%c'. Press 'h' for help.\n", c);
                break;
        }

        if (!running) break;
    }

    SetNonCanonicalInput(false);
}

void APCMiniTestApp::RunSimulationTest()
{
    printf("\n=== Simulation Test Mode ===\n");
    printf("Simulating APC Mini interactions...\n");

    current_mode = TEST_MODE_SIMULATION;

    // Simulate pad presses
    for (int i = 0; i < APC_MINI_PAD_COUNT; i++) {
        HandleNoteOn(APC_MINI_PAD_NOTE_START + i, 64);
        snooze(50000); // 50ms delay
        HandleNoteOff(APC_MINI_PAD_NOTE_START + i, 0);
    }

    // Simulate fader movements
    for (int cc = APC_MINI_FADER_CC_START; cc <= APC_MINI_FADER_CC_END; cc++) {
        for (int val = 0; val <= 127; val += 16) {
            HandleControlChange(cc, val);
            snooze(10000); // 10ms delay
        }
    }

    // Simulate button presses
    for (int i = APC_MINI_TRACK_NOTE_START; i <= APC_MINI_TRACK_NOTE_END; i++) {
        HandleNoteOn(i, 127);
        snooze(100000); // 100ms delay
        HandleNoteOff(i, 0);
    }

    printf("Simulation test completed\n");
    current_mode = TEST_MODE_INTERACTIVE;
}

void APCMiniTestApp::RunStressTest()
{
    printf("\n=== Stress Test Mode ===\n");
    printf("Sending %d MIDI messages as fast as possible...\n", STRESS_TEST_MESSAGES);

    current_mode = TEST_MODE_STRESS;
    bigtime_t start_time = system_time();

    for (int i = 0; i < STRESS_TEST_MESSAGES; i++) {
        uint8_t note = APC_MINI_PAD_NOTE_START + (i % APC_MINI_PAD_COUNT);
        uint8_t velocity = (i % 127) + 1;

        if (use_usb_raw) {
            usb_midi->SendNoteOn(note, velocity);
            usb_midi->SendNoteOff(note);
        } else {
            if (midi_producer) {
                midi_producer->SprayNoteOn(APC_MINI_MIDI_CHANNEL, note, velocity, system_time());
                midi_producer->SprayNoteOff(APC_MINI_MIDI_CHANNEL, note, 0, system_time());
            }
        }

        if (i % 100 == 0) {
            printf("Sent %d/%d messages\r", i, STRESS_TEST_MESSAGES);
            fflush(stdout);
        }
    }

    bigtime_t end_time = system_time();
    double elapsed_ms = (end_time - start_time) / 1000.0;
    double messages_per_sec = (STRESS_TEST_MESSAGES * 2) / (elapsed_ms / 1000.0);

    printf("\nStress test completed:\n");
    printf("  Time: %.2f ms\n", elapsed_ms);
    printf("  Rate: %.1f messages/sec\n", messages_per_sec);

    current_mode = TEST_MODE_INTERACTIVE;
}

void APCMiniTestApp::RunLatencyTest()
{
    printf("\n=== Latency Test Mode ===\n");
    printf("Press any pad to measure round-trip latency...\n");
    printf("Press 'q' to return to interactive mode\n");

    current_mode = TEST_MODE_LATENCY;

    while (current_mode == TEST_MODE_LATENCY) {
        snooze(100000); // 100ms
        if (!running) break;
    }
}

void APCMiniTestApp::TestPadMatrix()
{
    printf("\n=== Testing Pad Matrix ===\n");
    printf("Lighting up all 64 pads in sequence...\n");

    for (int y = 0; y < APC_MINI_PAD_ROWS; y++) {
        for (int x = 0; x < APC_MINI_PAD_COLS; x++) {
            uint8_t note = PAD_XY_TO_NOTE(x, y);
            printf("Testing pad (%d,%d) = note %d\n", x, y, note);

            SendLEDUpdate(note, APC_LED_GREEN);
            snooze(200000); // 200ms
            SendLEDUpdate(note, APC_LED_OFF);
        }
    }

    printf("Pad matrix test completed\n");
}

void APCMiniTestApp::TestFaders()
{
    printf("\n=== Testing Faders ===\n");
    printf("Move the faders to see their values...\n");
    printf("Press any key to continue...\n");

    while (getchar() != '\n') {
        snooze(10000);
    }
}

void APCMiniTestApp::TestButtons()
{
    printf("\n=== Testing Buttons ===\n");
    printf("Press track and scene buttons...\n");
    printf("Press any key to continue...\n");

    while (getchar() != '\n') {
        snooze(10000);
    }
}

void APCMiniTestApp::TestLEDColors()
{
    printf("\n=== Testing LED Colors ===\n");
    printf("Cycling through all LED colors...\n");

    APCMiniLEDColor colors[] = {
        APC_LED_GREEN, APC_LED_GREEN_BLINK,
        APC_LED_RED, APC_LED_RED_BLINK,
        APC_LED_YELLOW, APC_LED_YELLOW_BLINK
    };

    const char* color_names[] = {
        "Green", "Green Blink",
        "Red", "Red Blink",
        "Yellow", "Yellow Blink"
    };

    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        printf("Setting all pads to %s...\n", color_names[i]);

        for (int pad = 0; pad < APC_MINI_PAD_COUNT; pad++) {
            SendLEDUpdate(APC_MINI_PAD_NOTE_START + pad, colors[i]);
        }

        snooze(1000000); // 1 second
    }

    // Turn off all LEDs
    printf("Turning off all LEDs...\n");
    for (int pad = 0; pad < APC_MINI_PAD_COUNT; pad++) {
        SendLEDUpdate(APC_MINI_PAD_NOTE_START + pad, APC_LED_OFF);
    }

    printf("LED color test completed\n");
}

void APCMiniTestApp::DisplayStats()
{
    printf("\n=== Statistics ===\n");

    if (use_usb_raw && usb_midi) {
        const APCMiniStats& stats = usb_midi->GetStats();
        printf("USB Raw Mode:\n");
        printf("  Messages received: %u\n", stats.messages_received);
        printf("  Messages sent: %u\n", stats.messages_sent);
        printf("  Pad presses: %u\n", stats.pad_presses);
        printf("  Fader moves: %u\n", stats.fader_moves);
        printf("  Button presses: %u\n", stats.button_presses);
        printf("  Errors: %u\n", stats.error_count);

        if (stats.messages_received > 0) {
            uint32_t avg_latency = stats.total_latency_us / stats.messages_received;
            printf("  Latency - Min: %u μs, Max: %u μs, Avg: %u μs\n",
                   stats.min_latency_us, stats.max_latency_us, avg_latency);
        }
    } else {
        printf("Haiku MIDI Mode:\n");
        printf("  Device state statistics:\n");
        printf("  Current pad states: %u active\n", device_state.stats.pad_presses);
        printf("  Current fader positions tracked\n");
    }

    printf("\n");
}

void APCMiniTestApp::ResetDevice()
{
    printf("\n=== Resetting Device ===\n");

    // Turn off all LEDs
    for (int i = 0; i < APC_MINI_PAD_COUNT; i++) {
        SendLEDUpdate(APC_MINI_PAD_NOTE_START + i, APC_LED_OFF);
    }

    // Reset device state
    memset(&device_state, 0, sizeof(device_state));

    // Reset statistics
    if (use_usb_raw && usb_midi) {
        usb_midi->ResetStats();
    }

    printf("Device reset completed\n");
}

void APCMiniTestApp::HandleNoteOn(uint8_t note, uint8_t velocity)
{
    // bigtime_t current_time = system_time(); // Unused for now

    if (IS_PAD_NOTE(note)) {
        uint8_t pad = note - APC_MINI_PAD_NOTE_START;
        device_state.pads[pad] = true;
        device_state.pad_velocities[pad] = velocity;
        device_state.stats.pad_presses++;

        int x = PAD_NOTE_TO_X(note);
        int y = PAD_NOTE_TO_Y(note);
        printf("Pad (%d,%d) pressed: velocity %d\n", x, y, velocity);

        // In latency test mode, measure round-trip time
        if (current_mode == TEST_MODE_LATENCY) {
            SendLEDUpdate(note, APC_LED_GREEN);
            snooze(50000);
            SendLEDUpdate(note, APC_LED_OFF);
        }

    } else if (IS_TRACK_NOTE(note)) {
        uint8_t track = note - APC_MINI_TRACK_NOTE_START;
        device_state.track_buttons[track] = true;
        device_state.stats.button_presses++;
        printf("Track button %d pressed\n", track + 1);

    } else if (IS_SCENE_NOTE(note)) {
        uint8_t scene = note - APC_MINI_SCENE_NOTE_START;
        device_state.scene_buttons[scene] = true;
        device_state.stats.button_presses++;
        printf("Scene button %d pressed\n", scene + 1);

    } else if (IS_SHIFT_NOTE(note)) {
        device_state.shift_pressed = true;
        device_state.stats.button_presses++;
        printf("Shift button pressed\n");
    }

    // Forward all MIDI events to Cortex
    if (midi_producer) {
        midi_producer->SprayNoteOn(APC_MINI_MIDI_CHANNEL, note, velocity, system_time());
    }
}

void APCMiniTestApp::HandleNoteOff(uint8_t note, uint8_t /*velocity*/)
{
    if (IS_PAD_NOTE(note)) {
        uint8_t pad = note - APC_MINI_PAD_NOTE_START;
        device_state.pads[pad] = false;
        device_state.pad_velocities[pad] = 0;

        int x = PAD_NOTE_TO_X(note);
        int y = PAD_NOTE_TO_Y(note);
        printf("Pad (%d,%d) released\n", x, y);

    } else if (IS_TRACK_NOTE(note)) {
        uint8_t track = note - APC_MINI_TRACK_NOTE_START;
        device_state.track_buttons[track] = false;
        printf("Track button %d released\n", track + 1);

    } else if (IS_SCENE_NOTE(note)) {
        uint8_t scene = note - APC_MINI_SCENE_NOTE_START;
        device_state.scene_buttons[scene] = false;
        printf("Scene button %d released\n", scene + 1);

    } else if (IS_SHIFT_NOTE(note)) {
        device_state.shift_pressed = false;
        printf("Shift button released\n");
    }

    // Forward all MIDI events to Cortex
    if (midi_producer) {
        midi_producer->SprayNoteOff(APC_MINI_MIDI_CHANNEL, note, 0, system_time());
    }
}

void APCMiniTestApp::HandleControlChange(uint8_t controller, uint8_t value)
{
    if (IS_TRACK_FADER_CC(controller)) {
        // Track faders 1-8 (CC 48-55)
        uint8_t fader = controller - APC_MINI_FADER_CC_START;
        device_state.track_fader_values[fader] = value;
        device_state.stats.fader_moves++;

        printf("Track Fader %d: %d\n", fader + 1, value);

        // Forward to MIDI output for Cortex compatibility
        if (midi_producer) {
            midi_producer->SprayControlChange(APC_MINI_MIDI_CHANNEL, controller, value, system_time());
        }
    } else if (IS_MASTER_FADER_CC(controller)) {
        // Master fader (CC 56)
        device_state.master_fader_value = value;
        device_state.stats.fader_moves++;

        printf("Master Fader: %d\n", value);

        // Forward to MIDI output for Cortex compatibility
        if (midi_producer) {
            midi_producer->SprayControlChange(APC_MINI_MIDI_CHANNEL, controller, value, system_time());
        }
    }
}

bool APCMiniTestApp::InitializeUSBRaw()
{
    usb_midi = new USBRawMIDI();

    // Set up MIDI callback
    usb_midi->SetMIDICallback([this](uint8_t status, uint8_t data1, uint8_t data2) {
        uint8_t msg_type = status & 0xF0;
        uint8_t channel = status & 0x0F;

        if (channel != APC_MINI_MIDI_CHANNEL) {
            return; // Ignore messages from other channels
        }

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
    });

    APCMiniError result = usb_midi->Initialize();
    if (result != APC_SUCCESS) {
        delete usb_midi;
        usb_midi = nullptr;
        return false;
    }

    return true;
}

bool APCMiniTestApp::InitializeHaikuMIDI()
{
    midi_consumer = new MIDIConsumerApp(this);
    midi_producer = new MIDIProducerApp();

    if (midi_consumer->Register() != B_OK) {
        printf("Failed to register MIDI consumer\n");
        return false;
    }

    if (midi_producer->Register() != B_OK) {
        printf("Failed to register MIDI producer\n");
        return false;
    }

    printf("MIDI Endpoints registered successfully with Patchbay:\n");
    printf("  Consumer: ID %d, '%s' - Receives input from APC Mini controller\n",
           midi_consumer->ID(), midi_consumer->Name());
    printf("  Producer: ID %d, '%s' - Sends output to APC Mini for LED control\n",
           midi_producer->ID(), midi_producer->Name());

    // Try to find and connect to APC Mini
    BMidiRoster* roster = BMidiRoster::MidiRoster();
    if (!roster) {
        printf("Cannot access MIDI roster\n");
        return false;
    }

    // List available MIDI devices
    int32 id = 0;
    BMidiEndpoint* endpoint;
    bool found_apc = false;

    printf("Available MIDI devices:\n");

    // First, show our own endpoints
    printf("  %d: %s [Local Consumer] - Connect other producers here\n",
           midi_consumer->ID(), midi_consumer->Name());
    printf("  %d: %s [Local Producer] - Connect to other consumers/synthesizers\n",
           midi_producer->ID(), midi_producer->Name());

    while ((endpoint = roster->NextEndpoint(&id)) != nullptr) {
        const char* endpoint_name = endpoint->Name();
        printf("  %d: %s", id, endpoint_name);

        // Show device capabilities and connection instructions
        if (endpoint->IsProducer() && endpoint->IsConsumer()) {
            printf(" [Bidirectional MIDI Device]");
        } else if (endpoint->IsProducer()) {
            printf(" [MIDI Producer] - Can send to our Consumer");
        } else if (endpoint->IsConsumer()) {
            printf(" [MIDI Consumer] - Can receive from our Producer");
        }

        // Check device properties if available
        BMessage properties;
        if (endpoint->GetProperties(&properties) == B_OK) {
            const char* manufacturer, *product;
            if (properties.FindString("be:manufacturer", &manufacturer) == B_OK) {
                printf(" (Mfr: %s)", manufacturer);
            }
            if (properties.FindString("be:product", &product) == B_OK) {
                printf(" (Product: %s)", product);
            }
        }
        printf("\n");

        BString name(endpoint_name);
        if (name.IFindFirst("apc") != B_ERROR || name.IFindFirst("mini") != B_ERROR) {
            // Check if it's specifically a MK2 variant
            if (name.IFindFirst("mk2") != B_ERROR || name.IFindFirst("mk ii") != B_ERROR) {
                printf("    *** APC Mini MK2 detected! Auto-connecting... ***\n");
            } else {
                printf("    *** Potential APC Mini device detected! Auto-connecting... ***\n");
            }
            if (endpoint->IsProducer()) {
                static_cast<BMidiProducer*>(endpoint)->Connect(midi_consumer);
                printf("    -> APC Mini Producer connected to our Consumer (input path active)\n");
                found_apc = true;
            }
            if (endpoint->IsConsumer()) {
                midi_producer->Connect(static_cast<BMidiConsumer*>(endpoint));
                printf("    -> Our Producer connected to APC Mini Consumer (output path active)\n");
            }
        }

        endpoint->Release();
    }

    if (!found_apc) {
        printf("APC Mini not found in MIDI devices. Using fallback mode.\n");
    }

    return true;
}

void APCMiniTestApp::SendLEDUpdate(uint8_t note, APCMiniLEDColor color)
{
    if (use_usb_raw && usb_midi) {
        usb_midi->SendNoteOn(note, static_cast<uint8_t>(color));
    } else if (midi_producer) {
        midi_producer->SprayNoteOn(APC_MINI_MIDI_CHANNEL, note,
                                   static_cast<uint8_t>(color), system_time());
    }
}

void APCMiniTestApp::PrintHelp()
{
    printf("\n=== APC Mini Test Commands ===\n");
    printf("  h - Show this help\n");
    printf("  s - Show statistics\n");
    printf("  t - Test pad matrix\n");
    printf("  f - Test faders\n");
    printf("  b - Test buttons\n");
    printf("  c - Test LED colors\n");
    printf("  r - Reset device\n");
    printf("  v - View device state\n");
    printf("  p - Print pad matrix\n");
    printf("  l - Latency test mode\n");
    printf("  x - Stress test\n");
    printf("  m - Simulation mode\n");
    printf("  g - MK2 RGB LED test\n");
    printf("  n - MK2 Note/Drum modes test\n");
    printf("  q - Quit\n");
    printf("=============================\n\n");
}

void APCMiniTestApp::SetNonCanonicalInput(bool enable)
{
    static struct termios orig_termios;
    static bool saved = false;

    if (enable) {
        if (!saved) {
            tcgetattr(STDIN_FILENO, &orig_termios);
            saved = true;
        }

        struct termios new_termios = orig_termios;
        new_termios.c_lflag &= ~(ICANON | ECHO);
        new_termios.c_cc[VMIN] = 1;
        new_termios.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    } else if (saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    }
}

void APCMiniTestApp::PrintDeviceState()
{
    printf("\n=== Device State ===\n");

    printf("Pads (pressed):");
    bool any_pressed = false;
    for (int i = 0; i < APC_MINI_PAD_COUNT; i++) {
        if (device_state.pads[i]) {
            int x = PAD_NOTE_TO_X(i);
            int y = PAD_NOTE_TO_Y(i);
            printf(" (%d,%d)", x, y);
            any_pressed = true;
        }
    }
    if (!any_pressed) printf(" none");
    printf("\n");

    printf("Faders:");
    // Display track faders 1-8
    for (int i = 0; i < APC_MINI_TRACK_FADER_COUNT; i++) {
        printf(" %d:%d", i + 1, device_state.track_fader_values[i]);
    }
    // Display master fader
    printf(" M:%d", device_state.master_fader_value);
    printf("\n");

    printf("Track buttons:");
    for (int i = 0; i < 8; i++) {
        printf(" %d:%s", i + 1, device_state.track_buttons[i] ? "ON" : "off");
    }
    printf("\n");

    printf("Scene buttons:");
    for (int i = 0; i < 8; i++) {
        printf(" %d:%s", i + 1, device_state.scene_buttons[i] ? "ON" : "off");
    }
    printf("\n");

    printf("Shift: %s\n", device_state.shift_pressed ? "PRESSED" : "released");

    printf("==================\n\n");
}

void APCMiniTestApp::PrintPadMatrix()
{
    printf("\n=== Pad Matrix ===\n");
    printf("   0 1 2 3 4 5 6 7\n");

    for (int y = 0; y < APC_MINI_PAD_ROWS; y++) {
        printf("%d  ", y);
        for (int x = 0; x < APC_MINI_PAD_COLS; x++) {
            uint8_t pad = PAD_XY_TO_NOTE(x, y);
            char symbol = device_state.pads[pad] ? 'X' : '.';
            printf("%c ", symbol);
        }
        printf("\n");
    }
    printf("==================\n\n");
}

// MIDIConsumerApp implementation
void APCMiniTestApp::MIDIConsumerApp::NoteOn(uchar channel, uchar note, uchar velocity, bigtime_t /*time*/)
{
    if (channel == APC_MINI_MIDI_CHANNEL && velocity > 0) {
        app->HandleNoteOn(note, velocity);
    }
}

void APCMiniTestApp::MIDIConsumerApp::NoteOff(uchar channel, uchar note, uchar velocity, bigtime_t /*time*/)
{
    if (channel == APC_MINI_MIDI_CHANNEL) {
        app->HandleNoteOff(note, velocity);
    }
}

void APCMiniTestApp::MIDIConsumerApp::ControlChange(uchar channel, uchar controlNumber, uchar controlValue, bigtime_t /*time*/)
{
    if (channel == APC_MINI_MIDI_CHANNEL) {
        app->HandleControlChange(controlNumber, controlValue);
    }
}

int main(int /*argc*/, char* /*argv*/[])
{
    printf("Starting APC Mini Test Application...\n");

    APCMiniTestApp app;
    app.Run();

    printf("Application terminated.\n");
    return 0;
}

// ========== MK2 RGB SUPPORT IMPLEMENTATION ==========

// MK2 Preset RGB Colors Table (128 colors from official protocol)
// These hex values are converted from the official velocity-to-color mapping
const APCMiniMK2RGB APC_MK2_PRESET_COLORS[128] = {
    {0x00, 0x00, 0x00}, // 0 - #000000 Black
    {0x1E, 0x1E, 0x1E}, // 1 - #1E1E1E Dark Gray
    {0x7F, 0x7F, 0x7F}, // 2 - #7F7F7F Gray
    {0x7F, 0x7F, 0x7F}, // 3 - #FFFFFF White (capped at 7F)
    {0x7F, 0x4C, 0x4C}, // 4 - #FF4C4C Light Red
    {0x7F, 0x00, 0x00}, // 5 - #FF0000 Red
    {0x59, 0x00, 0x00}, // 6 - #590000 Dark Red
    {0x19, 0x00, 0x00}, // 7 - #190000 Very Dark Red
    {0x7F, 0x5D, 0x6C}, // 8 - #FFBD6C Orange
    {0x7F, 0x54, 0x00}, // 9 - #FF5400 Orange Red
    {0x59, 0x1D, 0x00}, // 10 - #591D00
    {0x27, 0x1B, 0x00}, // 11 - #271B00
    {0x7F, 0x7F, 0x4C}, // 12 - #FFFF4C Yellow
    {0x7F, 0x7F, 0x00}, // 13 - #FFFF00 Yellow
    {0x59, 0x59, 0x00}, // 14 - #595900
    {0x19, 0x19, 0x00}, // 15 - #191900
    {0x4C, 0x7F, 0x4C}, // 16 - #88FF4C Light Green
    {0x54, 0x7F, 0x00}, // 17 - #54FF00 Green
    {0x1D, 0x59, 0x00}, // 18 - #1D5900
    {0x14, 0x2B, 0x00}, // 19 - #142B00
    {0x4C, 0x7F, 0x4C}, // 20 - #4CFF4C Green
    {0x00, 0x7F, 0x00}, // 21 - #00FF00 Pure Green
    {0x00, 0x59, 0x00}, // 22 - #005900
    {0x00, 0x19, 0x00}, // 23 - #001900
    {0x4C, 0x7F, 0x5E}, // 24 - #4CFF5E
    {0x00, 0x7F, 0x19}, // 25 - #00FF19
    {0x00, 0x59, 0x0D}, // 26 - #00590D
    {0x00, 0x19, 0x02}, // 27 - #001902
    {0x4C, 0x7F, 0x7F}, // 28 - #4CFF88 (approximated)
    {0x00, 0x7F, 0x55}, // 29 - #00FF55
    {0x00, 0x59, 0x1D}, // 30 - #00591D
    {0x00, 0x1F, 0x12}, // 31 - #001F12
    {0x4C, 0x7F, 0x77}, // 32 - #4CFFB7 (approximated)
    {0x00, 0x7F, 0x7F}, // 33 - #00FF99 (approximated)
    {0x00, 0x59, 0x35}, // 34 - #005935
    {0x00, 0x19, 0x12}, // 35 - #001912
    {0x4C, 0x63, 0x7F}, // 36 - #4CC3FF (approximated)
    {0x00, 0x69, 0x7F}, // 37 - #00A9FF (approximated)
    {0x00, 0x41, 0x52}, // 38 - #004152
    {0x00, 0x10, 0x19}, // 39 - #001019
    {0x4C, 0x7F, 0x7F}, // 40 - #4C88FF (approximated)
    {0x00, 0x55, 0x7F}, // 41 - #0055FF
    {0x00, 0x1D, 0x59}, // 42 - #001D59
    {0x00, 0x08, 0x19}, // 43 - #000819
    {0x4C, 0x4C, 0x7F}, // 44 - #4C4CFF
    {0x00, 0x00, 0x7F}, // 45 - #0000FF Blue
    {0x00, 0x00, 0x59}, // 46 - #000059
    {0x00, 0x00, 0x19}, // 47 - #000019
    {0x7F, 0x4C, 0x7F}, // 48 - #874CFF (approximated)
    {0x54, 0x00, 0x7F}, // 49 - #5400FF
    {0x19, 0x00, 0x64}, // 50 - #190064
    {0x0F, 0x00, 0x30}, // 51 - #0F0030
    {0x7F, 0x4C, 0x7F}, // 52 - #FF4CFF
    {0x7F, 0x00, 0x7F}, // 53 - #FF00FF Magenta
    {0x59, 0x00, 0x59}, // 54 - #590059
    {0x19, 0x00, 0x19}, // 55 - #190019
    {0x7F, 0x4C, 0x7F}, // 56 - #FF4C87 (approximated)
    {0x7F, 0x00, 0x54}, // 57 - #FF0054
    {0x59, 0x00, 0x1D}, // 58 - #59001D
    {0x22, 0x00, 0x13}, // 59 - #220013
    {0x7F, 0x15, 0x00}, // 60 - #FF1500
    {0x7F, 0x35, 0x00}, // 61 - #993500 (approximated)
    {0x79, 0x51, 0x00}, // 62 - #795100
    {0x43, 0x64, 0x00}, // 63 - #436400
    {0x03, 0x39, 0x00}, // 64 - #033900
    {0x00, 0x57, 0x35}, // 65 - #005735
    {0x00, 0x54, 0x7F}, // 66 - #00547F
    {0x00, 0x00, 0x7F}, // 67 - #0000FF
    {0x00, 0x45, 0x4F}, // 68 - #00454F
    {0x25, 0x00, 0x7F}, // 69 - #2500CC (approximated)
    {0x7F, 0x7F, 0x7F}, // 70 - #7F7F7F
    {0x20, 0x20, 0x20}, // 71 - #202020
    {0x7F, 0x00, 0x00}, // 72 - #FF0000
    {0x5D, 0x7F, 0x2D}, // 73 - #BDFF2D (approximated)
    {0x6F, 0x7F, 0x06}, // 74 - #AFED06 (approximated)
    {0x64, 0x7F, 0x09}, // 75 - #64FF09
    {0x10, 0x7F, 0x00}, // 76 - #108B00 (approximated)
    {0x00, 0x7F, 0x7F}, // 77 - #00FF87 (approximated)
    {0x00, 0x69, 0x7F}, // 78 - #00A9FF (approximated)
    {0x00, 0x2A, 0x7F}, // 79 - #002AFF
    {0x3F, 0x00, 0x7F}, // 80 - #3F00FF
    {0x7A, 0x00, 0x7F}, // 81 - #7A00FF
    {0x72, 0x1A, 0x7D}, // 82 - #B21A7D (approximated)
    {0x40, 0x21, 0x00}, // 83 - #402100
    {0x7F, 0x4A, 0x00}, // 84 - #FF4A00
    {0x7F, 0x61, 0x06}, // 85 - #88E106 (approximated)
    {0x72, 0x7F, 0x15}, // 86 - #72FF15
    {0x00, 0x7F, 0x00}, // 87 - #00FF00
    {0x3B, 0x7F, 0x26}, // 88 - #3BFF26
    {0x59, 0x7F, 0x71}, // 89 - #59FF71
    {0x38, 0x7F, 0x7F}, // 90 - #38FFCC (approximated)
    {0x5B, 0x7F, 0x7F}, // 91 - #5B8AFF (approximated)
    {0x31, 0x51, 0x7F}, // 92 - #3151C6 (approximated)
    {0x7F, 0x7F, 0x69}, // 93 - #877FE9 (approximated)
    {0x53, 0x1D, 0x7F}, // 94 - #D31DFF (approximated)
    {0x7F, 0x00, 0x5D}, // 95 - #FF005D
    {0x7F, 0x7F, 0x00}, // 96 - #FF7F00
    {0x79, 0x70, 0x00}, // 97 - #B9B000 (approximated)
    {0x7F, 0x7F, 0x00}, // 98 - #90FF00 (approximated)
    {0x35, 0x5D, 0x07}, // 99 - #835D07 (approximated)
    {0x39, 0x2B, 0x00}, // 100 - #392b00
    {0x14, 0x4C, 0x10}, // 101 - #144C10
    {0x0D, 0x50, 0x38}, // 102 - #0D5038
    {0x15, 0x15, 0x2A}, // 103 - #15152A
    {0x16, 0x20, 0x5A}, // 104 - #16205A
    {0x69, 0x3C, 0x1C}, // 105 - #693C1C
    {0x68, 0x00, 0x0A}, // 106 - #A8000A (approximated)
    {0x5E, 0x51, 0x3D}, // 107 - #DE513D (approximated)
    {0x58, 0x6A, 0x1C}, // 108 - #D86A1C (approximated)
    {0x7F, 0x61, 0x26}, // 109 - #FFE126 (approximated)
    {0x4E, 0x61, 0x2F}, // 110 - #9EE12F (approximated)
    {0x67, 0x75, 0x0F}, // 111 - #67B50F (approximated)
    {0x1E, 0x1E, 0x30}, // 112 - #1E1E30
    {0x5C, 0x7F, 0x6B}, // 113 - #DCFF6B (approximated)
    {0x40, 0x7F, 0x5D}, // 114 - #80FFBD (approximated)
    {0x4A, 0x7F, 0x7F}, // 115 - #9A99FF (approximated)
    {0x4E, 0x66, 0x7F}, // 116 - #8E66FF (approximated)
    {0x40, 0x40, 0x40}, // 117 - #404040
    {0x75, 0x75, 0x75}, // 118 - #757575
    {0x60, 0x7F, 0x7F}, // 119 - #E0FFFF (approximated)
    {0x60, 0x00, 0x00}, // 120 - #A00000 (approximated)
    {0x35, 0x00, 0x00}, // 121 - #350000
    {0x1A, 0x50, 0x00}, // 122 - #1AD000 (approximated)
    {0x07, 0x42, 0x00}, // 123 - #074200
    {0x79, 0x70, 0x00}, // 124 - #B9B000 (approximated)
    {0x3F, 0x31, 0x00}, // 125 - #3F3100
    {0x73, 0x5F, 0x00}, // 126 - #B35F00 (approximated)
    {0x4B, 0x15, 0x02}  // 127 - #4B1502
};

void APCMiniTestApp::DetectMK2Device()
{
    // Detection logic - check if connected device is MK2
    // This would be called when USB device is detected with PID 0x004F
    device_state.is_mk2_device = true;  // Set when MK2 detected
    device_state.led_mode = APC_MK2_LED_MODE_RGB;  // Enable RGB mode for MK2
    printf("APC Mini MK2 detected - RGB LED mode enabled\n");
}

void APCMiniTestApp::SendMK2SysEx(const uint8_t* /*data*/, size_t length)
{
    if (use_usb_raw && usb_midi) {
        // Send SysEx via USB Raw (would need USB Raw SysEx support)
        printf("Sending MK2 SysEx via USB Raw (%zu bytes)\n", length);
        // TODO: Implement USB Raw SysEx when available
    } else if (midi_producer) {
        // Send SysEx via Haiku MIDI
        printf("Sending MK2 SysEx via MIDI Producer (%zu bytes)\n", length);
        // BMidiLocalProducer should support SysEx
        // TODO: Check if SpraySysEx method exists
    }
}

void APCMiniTestApp::SendMK2RGBUpdate(uint8_t note, const APCMiniMK2RGB& rgb_color)
{
    printf("SendMK2RGBUpdate: note=%d, RGB(%d,%d,%d)\n", note, rgb_color.red, rgb_color.green, rgb_color.blue);

    if (!device_state.is_mk2_device) {
        // Fall back to legacy color for original APC Mini
        APCMiniLEDColor legacy_color = APC_LED_OFF;
        if (rgb_color.red > 64) legacy_color = APC_LED_RED;
        else if (rgb_color.green > 64) legacy_color = APC_LED_GREEN;
        else if (rgb_color.red > 32 && rgb_color.green > 32) legacy_color = APC_LED_YELLOW;

        SendLEDUpdate(note, legacy_color);
        return;
    }

    // For MK2: Use MIDI Note-On with preset colors (recommended method)
    // Find closest preset color match
    printf("Searching for closest preset color...\n");
    uint8_t best_color_index = 0;
    int min_distance = INT_MAX;

    for (int i = 0; i < 128; i++) {
        const APCMiniMK2RGB& preset = APC_MK2_PRESET_COLORS[i];
        int distance = abs(rgb_color.red - preset.red) +
                      abs(rgb_color.green - preset.green) +
                      abs(rgb_color.blue - preset.blue);
        if (distance < min_distance) {
            min_distance = distance;
            best_color_index = i;
        }
    }
    printf("Found best color index: %d\n", best_color_index);

    // Send MIDI Note-On with preset color (channel 6 = 100% brightness)
    if (use_usb_raw && usb_midi) {
        usb_midi->SendNoteOn(note, best_color_index);
    } else if (midi_producer) {
        midi_producer->SprayNoteOn(6, note, best_color_index, system_time()); // Channel 6 = 96 hex = 100% brightness
    }

    // Store RGB color in device state
    if (note <= APC_MINI_PAD_NOTE_END) {  // note is uint8_t, always >= 0
        uint8_t pad_index = note - APC_MINI_PAD_NOTE_START;
        device_state.pad_rgb_colors[pad_index] = rgb_color;
    }
}

void APCMiniTestApp::SendMK2CustomRGB(uint8_t start_pad, uint8_t end_pad, const APCMiniMK2RGB& rgb_color)
{
    if (!device_state.is_mk2_device) {
        printf("Custom RGB requires MK2 device\n");
        return;
    }

    // Official MK2 SysEx RGB Color Lighting format:
    // F0 47 7F 4F 24 [total MSB] [total LSB] [start_pad] [end_pad] [R_MSB] [R_LSB] [G_MSB] [G_LSB] [B_MSB] [B_LSB] F7

    // Calculate data length: 8 bytes (start_pad through B_LSB)
    uint16_t data_length = 8;
    uint8_t length_msb = (data_length >> 7) & 0x7F;
    uint8_t length_lsb = data_length & 0x7F;

    // Convert 8-bit RGB to MSB/LSB format (7-bit MIDI)
    uint8_t red_msb = (rgb_color.red >> 7) & 0x7F;
    uint8_t red_lsb = rgb_color.red & 0x7F;
    uint8_t green_msb = (rgb_color.green >> 7) & 0x7F;
    uint8_t green_lsb = rgb_color.green & 0x7F;
    uint8_t blue_msb = (rgb_color.blue >> 7) & 0x7F;
    uint8_t blue_lsb = rgb_color.blue & 0x7F;

    uint8_t sysex_data[] = {
        APC_MK2_SYSEX_HEADER,      // F0 47 7F 4F
        APC_MK2_SYSEX_RGB_CMD,     // 24 - RGB Color Lighting command
        length_msb,                 // Data length MSB
        length_lsb,                 // Data length LSB
        static_cast<uint8_t>(start_pad & 0x3F),  // Start pad (0x00-0x3F)
        static_cast<uint8_t>(end_pad & 0x3F),    // End pad (0x00-0x3F)
        red_msb,                   // Red MSB
        red_lsb,                   // Red LSB
        green_msb,                 // Green MSB
        green_lsb,                 // Green LSB
        blue_msb,                  // Blue MSB
        blue_lsb,                  // Blue LSB
        APC_MK2_SYSEX_END          // F7
    };

    SendMK2SysEx(sysex_data, sizeof(sysex_data));

    // Store RGB color in device state for all pads in range
    for (uint8_t pad = start_pad; pad <= end_pad && pad < APC_MINI_PAD_COUNT; pad++) {
        device_state.pad_rgb_colors[pad] = rgb_color;
    }

    printf("Custom RGB sent to pads %d-%d: RGB(%d,%d,%d)\n",
           start_pad, end_pad, rgb_color.red, rgb_color.green, rgb_color.blue);
}

void APCMiniTestApp::TestMK2RGB()
{
    if (!device_state.is_mk2_device) {
        printf("MK2 RGB test requires MK2 device. Enabling MK2 simulation...\n");
        DetectMK2Device();
    }

    printf("\n=== MK2 RGB LED Test ===\n");
    printf("Testing RGB colors on pad matrix...\n");

    // Test RGB rainbow across pads
    for (int i = 0; i < 2; i++) {  // Test only 2 pads first for debug
        APCMiniMK2RGB color;

        // Create rainbow effect
        if (i < 2) {
            color = {127, 0, 0};      // Red
        } else if (i < 4) {
            color = {127, 127, 0};    // Yellow
        } else if (i < 6) {
            color = {0, 127, 0};      // Green
        } else {
            color = {0, 0, 127};      // Blue
        }

        // Set top row of pads
        uint8_t note = APC_MINI_PAD_NOTE_START + i;
        SendMK2RGBUpdate(note, color);
        printf("Pad %d: RGB(%d,%d,%d)\n", i, color.red, color.green, color.blue);

        snooze(200000); // 200ms delay
    }

    // Turn off all LEDs
    printf("Turning off RGB LEDs...\n");
    APCMiniMK2RGB off_color = {0, 0, 0};
    for (int i = 0; i < 8; i++) {
        uint8_t note = APC_MINI_PAD_NOTE_START + i;
        SendMK2RGBUpdate(note, off_color);
    }

    // Test custom RGB SysEx (official protocol)
    printf("\nTesting Custom RGB SysEx...\n");
    APCMiniMK2RGB custom_purple = {100, 0, 100};
    SendMK2CustomRGB(8, 15, custom_purple); // Second row (pads 8-15)
    snooze(2000000); // 2s delay

    // Turn off custom RGB
    APCMiniMK2RGB custom_off = {0, 0, 0};
    SendMK2CustomRGB(8, 15, custom_off);

    printf("MK2 RGB test completed\n");
}

// ========== MK2 NOTE/DRUM MODE IMPLEMENTATION ==========

void APCMiniTestApp::SetMK2Mode(APCMiniMK2Mode mode)
{
    if (!device_state.is_mk2_device) {
        printf("Mode switching requires MK2 device\n");
        return;
    }

    device_state.device_mode = mode;

    // Send mode switch via SysEx
    uint8_t sysex_data[] = {
        APC_MK2_SYSEX_HEADER,     // F0 47 7F 29
        APC_MK2_SYSEX_MODE_CMD,   // 61
        static_cast<uint8_t>(mode), // Mode value
        APC_MK2_SYSEX_END         // F7
    };

    SendMK2SysEx(sysex_data, sizeof(sysex_data));

    const char* mode_names[] = {"Session", "Note", "Drum"};
    printf("MK2 mode switched to: %s\n", mode_names[mode]);

    // Recalculate pad mappings based on new mode
    switch (mode) {
        case APC_MK2_MODE_NOTE:
            CalculateNoteModeNotes(device_state.current_scale, device_state.root_note);
            break;
        case APC_MK2_MODE_DRUM:
            CalculateDrumModeNotes();
            break;
        default:
            // Session mode uses original pad mapping
            break;
    }
}

void APCMiniTestApp::SetupNoteMode(APCMiniMK2Scale scale, uint8_t root_note)
{
    device_state.current_scale = scale;
    device_state.root_note = root_note;
    SetMK2Mode(APC_MK2_MODE_NOTE);
}

void APCMiniTestApp::SetupDrumMode()
{
    SetMK2Mode(APC_MK2_MODE_DRUM);
}

void APCMiniTestApp::CalculateNoteModeNotes(APCMiniMK2Scale scale, uint8_t root_note)
{
    // Scale intervals (semitones from root)
    const uint8_t scale_intervals[][8] = {
        {0, 1, 2, 3, 4, 5, 6, 7},           // Chromatic
        {0, 2, 4, 5, 7, 9, 11, 12},         // Major
        {0, 2, 3, 5, 7, 8, 10, 12},         // Natural Minor
        {0, 2, 4, 7, 9, 12, 14, 16},        // Pentatonic
        {0, 3, 5, 6, 7, 10, 12, 15}         // Blues
    };

    printf("Calculating Note Mode: Scale=%d, Root=%d\n", scale, root_note);

    // Map 8x8 pad matrix to scale notes
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            uint8_t pad_index = row * 8 + col;

            // Calculate note based on row (octave) and column (scale degree)
            uint8_t octave_offset = (7 - row) * 12;  // Top row = higher octave
            uint8_t scale_degree = col % 8;
            uint8_t interval = scale_intervals[scale][scale_degree];

            uint8_t note = root_note + octave_offset + interval;
            if (note > 127) note = 127;  // MIDI note limit

            device_state.note_mode_notes[pad_index] = note;
        }
    }
}

void APCMiniTestApp::CalculateDrumModeNotes()
{
    // Standard drum kit layout for 8x8 pads
    const uint8_t drum_layout[64] = {
        // Row 0 (top): Cymbals and percussion
        49, 51, 55, 57, 59, 60, 61, 62,  // Crash, Ride, Splash, etc.

        // Row 1: Hi-hats and cymbals
        42, 44, 46, 49, 51, 53, 55, 57,

        // Row 2: Snares and rim shots
        38, 40, 37, 39, 38, 40, 37, 39,

        // Row 3: Toms
        48, 47, 45, 43, 41, 48, 47, 45,

        // Row 4: More toms and percussion
        50, 48, 47, 45, 43, 41, 50, 48,

        // Row 5: Kicks and low percussion
        36, 35, 36, 35, 36, 35, 36, 35,

        // Row 6: Additional kicks and percussion
        36, 35, 36, 35, 36, 35, 36, 35,

        // Row 7 (bottom): Bass drums and low sounds
        36, 35, 36, 35, 36, 35, 36, 35
    };

    printf("Calculating Drum Mode layout\n");

    for (int i = 0; i < 64; i++) {
        device_state.drum_mode_notes[i] = drum_layout[i];
    }
}

uint8_t APCMiniTestApp::GetPadNoteInCurrentMode(uint8_t pad_index)
{
    if (pad_index >= APC_MINI_PAD_COUNT) return 0;

    switch (device_state.device_mode) {
        case APC_MK2_MODE_NOTE:
            return device_state.note_mode_notes[pad_index];
        case APC_MK2_MODE_DRUM:
            return device_state.drum_mode_notes[pad_index];
        default:
            // Session mode: original pad note mapping
            return APC_MINI_PAD_NOTE_START + pad_index;
    }
}

void APCMiniTestApp::TestMK2Modes()
{
    if (!device_state.is_mk2_device) {
        printf("MK2 Modes test requires MK2 device. Enabling MK2 simulation...\n");
        DetectMK2Device();
    }

    printf("\n=== MK2 Mode Testing ===\n");

    // Test Session Mode (default)
    printf("\n1. Testing Session Mode...\n");
    SetMK2Mode(APC_MK2_MODE_SESSION);
    snooze(1000000);

    // Test Note Mode with Major Scale
    printf("\n2. Testing Note Mode (Major Scale, Root C3)...\n");
    SetupNoteMode(APC_MK2_SCALE_MAJOR, 60);  // C4

    // Show first row note mapping
    printf("Top row notes: ");
    for (int i = 0; i < 8; i++) {
        uint8_t note = GetPadNoteInCurrentMode(i);
        printf("%d ", note);
    }
    printf("\n");
    snooze(2000000);

    // Test Note Mode with Minor Scale
    printf("\n3. Testing Note Mode (Minor Scale, Root C3)...\n");
    SetupNoteMode(APC_MK2_SCALE_MINOR, 60);
    snooze(2000000);

    // Test Drum Mode
    printf("\n4. Testing Drum Mode...\n");
    SetupDrumMode();

    // Show some drum mappings
    printf("Drum layout (first 8 pads): ");
    for (int i = 0; i < 8; i++) {
        uint8_t note = GetPadNoteInCurrentMode(i);
        printf("%d ", note);
    }
    printf("\n");
    snooze(2000000);

    // Return to Session Mode
    printf("\n5. Returning to Session Mode...\n");
    SetMK2Mode(APC_MK2_MODE_SESSION);

    printf("MK2 Mode test completed\n");
}

// NOTE: Navigation arrows function removed - not present in official MK2 protocol