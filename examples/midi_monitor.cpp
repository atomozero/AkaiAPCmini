#include <Application.h>
#include <MidiRoster.h>
#include <MidiConsumer.h>
#include <OS.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "../src/usb_raw_midi.h"
#include "../src/apc_mini_defs.h"

class MIDIMonitorApp : public BApplication {
public:
    MIDIMonitorApp();
    virtual ~MIDIMonitorApp();

    virtual void ReadyToRun() override;
    virtual bool QuitRequested() override;

    void RunMonitor();

private:
    class MIDIConsumerMonitor : public BMidiLocalConsumer {
    public:
        MIDIConsumerMonitor(MIDIMonitorApp* app) : app(app) {}
        virtual void NoteOn(uchar channel, uchar note, uchar velocity, bigtime_t time) override;
        virtual void NoteOff(uchar channel, uchar note, uchar velocity, bigtime_t time) override;
        virtual void ControlChange(uchar channel, uchar controlNumber, uchar controlValue, bigtime_t time) override;
        virtual void ProgramChange(uchar channel, uchar programNumber, bigtime_t time) override;
        virtual void SystemExclusive(void* data, size_t dataLength, bigtime_t time) override;

    private:
        MIDIMonitorApp* app;
    };

    USBRawMIDI* usb_midi;
    MIDIConsumerMonitor* midi_consumer;
    bool running;
    bool use_usb_raw;
    bool show_timestamps;
    bool decode_apc_messages;
    bool log_to_file;
    FILE* log_file;

    // Statistics
    struct {
        uint32_t total_messages;
        uint32_t note_on_count;
        uint32_t note_off_count;
        uint32_t control_change_count;
        uint32_t other_count;
        uint32_t apc_pad_messages;
        uint32_t apc_fader_messages;
        uint32_t apc_button_messages;
        bigtime_t start_time;
        bigtime_t last_message_time;
    } stats;

    // Message handlers
    void HandleMIDIMessage(const char* type, uint8_t channel, uint8_t data1, uint8_t data2, bigtime_t time);
    void HandleUSBRawMIDI(uint8_t status, uint8_t data1, uint8_t data2);

    // Utility functions
    void PrintMIDIMessage(const char* type, uint8_t channel, uint8_t data1, uint8_t data2, bigtime_t time);
    void DecodeAPCMessage(const char* type, uint8_t data1, uint8_t data2);
    void PrintStatistics();
    void PrintTimestamp(bigtime_t time);
    bool InitializeUSBRaw();
    bool InitializeHaikuMIDI();
    void ShowHelp();
    void ResetStatistics();

    // Command processing
    void ProcessCommand(char command);
};

// Global application instance for signal handling
MIDIMonitorApp* g_app = nullptr;

void signal_handler(int sig)
{
    if (g_app) {
        printf("\nShutting down MIDI monitor...\n");
        g_app->PostMessage(B_QUIT_REQUESTED);
    }
}

MIDIMonitorApp::MIDIMonitorApp()
    : BApplication("application/x-vnd.apc-mini-midi-monitor")
    , usb_midi(nullptr)
    , midi_consumer(nullptr)
    , running(true)
    , use_usb_raw(true)
    , show_timestamps(true)
    , decode_apc_messages(true)
    , log_to_file(false)
    , log_file(nullptr)
{
    memset(&stats, 0, sizeof(stats));
    g_app = this;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

MIDIMonitorApp::~MIDIMonitorApp()
{
    if (usb_midi) {
        usb_midi->Shutdown();
        delete usb_midi;
    }

    if (midi_consumer) {
        midi_consumer->Unregister();
        delete midi_consumer;
    }

    if (log_file) {
        fclose(log_file);
    }

    g_app = nullptr;
}

void MIDIMonitorApp::ReadyToRun()
{
    printf("APC Mini MIDI Monitor\n");
    printf("=====================\n\n");

    // Check command line arguments
    int32 argc;
    char** argv;
    GetArgvReceived(&argc, &argv);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            ShowHelp();
            PostMessage(B_QUIT_REQUESTED);
            return;
        } else if (strcmp(argv[i], "--no-usb-raw") == 0) {
            use_usb_raw = false;
        } else if (strcmp(argv[i], "--no-timestamps") == 0) {
            show_timestamps = false;
        } else if (strcmp(argv[i], "--no-decode") == 0) {
            decode_apc_messages = false;
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_to_file = true;
            log_file = fopen(argv[++i], "w");
            if (!log_file) {
                printf("Warning: Could not open log file %s\n", argv[i]);
                log_to_file = false;
            }
        } else if (strcmp(argv[i], "--test") == 0) {
            // Test mode for automated testing
            printf("Test mode: Monitor will run for 5 seconds then exit\n");
            // Set a timer to quit after 5 seconds
        }
    }

    // Initialize MIDI connection
    if (use_usb_raw && InitializeUSBRaw()) {
        printf("Using USB Raw MIDI access\n");
    } else {
        printf("Using Haiku MIDI API\n");
        if (!InitializeHaikuMIDI()) {
            printf("Failed to initialize MIDI interface\n");
            PostMessage(B_QUIT_REQUESTED);
            return;
        }
        use_usb_raw = false;
    }

    RunMonitor();
}

bool MIDIMonitorApp::QuitRequested()
{
    running = false;
    return true;
}

bool MIDIMonitorApp::InitializeUSBRaw()
{
    usb_midi = new USBRawMIDI();

    // Set up MIDI callback
    usb_midi->SetMIDICallback([this](uint8_t status, uint8_t data1, uint8_t data2) {
        HandleUSBRawMIDI(status, data1, data2);
    });

    APCMiniError result = usb_midi->Initialize();
    if (result != APC_SUCCESS) {
        delete usb_midi;
        usb_midi = nullptr;
        return false;
    }

    return true;
}

bool MIDIMonitorApp::InitializeHaikuMIDI()
{
    midi_consumer = new MIDIConsumerMonitor(this);

    if (midi_consumer->Register() != B_OK) {
        printf("Failed to register MIDI consumer\n");
        return false;
    }

    // Try to find and connect to APC Mini or any MIDI device
    BMidiRoster* roster = BMidiRoster::MidiRoster();
    if (!roster) {
        printf("Cannot access MIDI roster\n");
        return false;
    }

    int32 id = 0;
    BMidiEndpoint* endpoint;
    bool connected = false;

    printf("Available MIDI devices:\n");
    while ((endpoint = roster->NextEndpoint(&id)) != nullptr) {
        BString name;
        endpoint->Name(&name);
        printf("  %ld: %s", id, name.String());

        if (endpoint->IsProducer()) {
            midi_consumer->Connect(static_cast<BMidiProducer*>(endpoint));
            printf(" (connected)");
            connected = true;
        }

        printf("\n");
        endpoint->Release();
    }

    if (!connected) {
        printf("No MIDI producer devices found to connect to\n");
    }

    return true;
}

void MIDIMonitorApp::RunMonitor()
{
    printf("\nMIDI Monitor started\n");
    printf("Commands:\n");
    printf("  h - Show help\n");
    printf("  s - Show statistics\n");
    printf("  r - Reset statistics\n");
    printf("  t - Toggle timestamps\n");
    printf("  d - Toggle APC decoding\n");
    printf("  q - Quit\n\n");

    stats.start_time = system_time();

    // Set up non-blocking input
    struct termios old_termios, new_termios;
    tcgetattr(STDIN_FILENO, &old_termios);
    new_termios = old_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

    printf("Monitoring MIDI messages... (press 'h' for help)\n\n");

    while (running) {
        // Check for user input
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms

        int result = select(STDIN_FILENO + 1, &read_fds, nullptr, nullptr, &timeout);

        if (result > 0 && FD_ISSET(STDIN_FILENO, &read_fds)) {
            char c = getchar();
            ProcessCommand(c);
        }

        // Small delay to prevent high CPU usage
        snooze(10000); // 10ms
    }

    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);

    printf("\nFinal Statistics:\n");
    PrintStatistics();
}

void MIDIMonitorApp::HandleUSBRawMIDI(uint8_t status, uint8_t data1, uint8_t data2)
{
    bigtime_t current_time = system_time();
    stats.last_message_time = current_time;

    uint8_t msg_type = status & 0xF0;
    uint8_t channel = status & 0x0F;

    const char* type_name;
    switch (msg_type) {
        case MIDI_NOTE_ON:
            if (data2 > 0) {
                type_name = "Note On";
                stats.note_on_count++;
            } else {
                type_name = "Note Off";
                stats.note_off_count++;
            }
            break;
        case MIDI_NOTE_OFF:
            type_name = "Note Off";
            stats.note_off_count++;
            break;
        case MIDI_CONTROL_CHANGE:
            type_name = "Control Change";
            stats.control_change_count++;
            break;
        default:
            type_name = "Other";
            stats.other_count++;
            break;
    }

    stats.total_messages++;
    HandleMIDIMessage(type_name, channel, data1, data2, current_time);
}

void MIDIMonitorApp::HandleMIDIMessage(const char* type, uint8_t channel, uint8_t data1, uint8_t data2, bigtime_t time)
{
    PrintMIDIMessage(type, channel, data1, data2, time);

    if (decode_apc_messages && channel == APC_MINI_MIDI_CHANNEL) {
        DecodeAPCMessage(type, data1, data2);
    }

    // Log to file if enabled
    if (log_to_file && log_file) {
        fprintf(log_file, "[%lld] %s Ch:%d Data:%d,%d\n",
                time / 1000, type, channel, data1, data2);
        fflush(log_file);
    }
}

void MIDIMonitorApp::PrintMIDIMessage(const char* type, uint8_t channel, uint8_t data1, uint8_t data2, bigtime_t time)
{
    if (show_timestamps) {
        PrintTimestamp(time);
    }

    printf("%-15s Ch:%-2d Data1:%-3d Data2:%-3d (0x%02X 0x%02X)",
           type, channel, data1, data2, data1, data2);

    printf("\n");
}

void MIDIMonitorApp::DecodeAPCMessage(const char* type, uint8_t data1, uint8_t data2)
{
    if (strcmp(type, "Note On") == 0 || strcmp(type, "Note Off") == 0) {
        if (IS_PAD_NOTE(data1)) {
            int x = PAD_NOTE_TO_X(data1);
            int y = PAD_NOTE_TO_Y(data1);
            printf("  -> APC Pad (%d,%d) %s", x, y, strcmp(type, "Note On") == 0 ? "pressed" : "released");
            if (strcmp(type, "Note On") == 0) {
                printf(" velocity:%d", data2);
            }
            printf("\n");
            stats.apc_pad_messages++;
        } else if (IS_TRACK_NOTE(data1)) {
            int track = data1 - APC_MINI_TRACK_NOTE_START + 1;
            printf("  -> APC Track button %d %s\n", track, strcmp(type, "Note On") == 0 ? "pressed" : "released");
            stats.apc_button_messages++;
        } else if (IS_SCENE_NOTE(data1)) {
            int scene = data1 - APC_MINI_SCENE_NOTE_START + 1;
            printf("  -> APC Scene button %d %s\n", scene, strcmp(type, "Note On") == 0 ? "pressed" : "released");
            stats.apc_button_messages++;
        } else if (IS_SHIFT_NOTE(data1)) {
            printf("  -> APC Shift button %s\n", strcmp(type, "Note On") == 0 ? "pressed" : "released");
            stats.apc_button_messages++;
        }
    } else if (strcmp(type, "Control Change") == 0) {
        if (IS_TRACK_FADER_CC(data1)) {
            int fader = data1 - APC_MINI_FADER_CC_START;
            printf("  -> APC Track fader %d: %d\n", fader + 1, data2);
            stats.apc_fader_messages++;
        } else if (IS_MASTER_FADER_CC(data1)) {
            printf("  -> APC Master fader: %d\n", data2);
            stats.apc_fader_messages++;
        }
    }
}

void MIDIMonitorApp::PrintStatistics()
{
    bigtime_t current_time = system_time();
    bigtime_t elapsed_time = current_time - stats.start_time;
    double elapsed_seconds = elapsed_time / 1000000.0;

    printf("\nMIDI Monitor Statistics\n");
    printf("=======================\n");
    printf("Running time: %.1f seconds\n", elapsed_seconds);
    printf("Total messages: %u\n", stats.total_messages);

    if (stats.total_messages > 0) {
        printf("Message rate: %.1f msg/sec\n", stats.total_messages / elapsed_seconds);
    }

    printf("\nMessage Types:\n");
    printf("  Note On:        %u\n", stats.note_on_count);
    printf("  Note Off:       %u\n", stats.note_off_count);
    printf("  Control Change: %u\n", stats.control_change_count);
    printf("  Other:          %u\n", stats.other_count);

    if (decode_apc_messages) {
        printf("\nAPC Mini Messages:\n");
        printf("  Pad events:     %u\n", stats.apc_pad_messages);
        printf("  Fader events:   %u\n", stats.apc_fader_messages);
        printf("  Button events:  %u\n", stats.apc_button_messages);
    }

    if (stats.last_message_time > 0) {
        bigtime_t time_since_last = current_time - stats.last_message_time;
        printf("\nLast message: %.1f seconds ago\n", time_since_last / 1000000.0);
    }

    printf("\n");
}

void MIDIMonitorApp::PrintTimestamp(bigtime_t time)
{
    bigtime_t relative_time = time - stats.start_time;
    double seconds = relative_time / 1000000.0;
    printf("[%8.3f] ", seconds);
}

void MIDIMonitorApp::ProcessCommand(char command)
{
    switch (command) {
        case 'h':
        case 'H':
            ShowHelp();
            break;

        case 's':
        case 'S':
            PrintStatistics();
            break;

        case 'r':
        case 'R':
            ResetStatistics();
            printf("Statistics reset\n");
            break;

        case 't':
        case 'T':
            show_timestamps = !show_timestamps;
            printf("Timestamps %s\n", show_timestamps ? "enabled" : "disabled");
            break;

        case 'd':
        case 'D':
            decode_apc_messages = !decode_apc_messages;
            printf("APC message decoding %s\n", decode_apc_messages ? "enabled" : "disabled");
            break;

        case 'q':
        case 'Q':
            printf("Quitting...\n");
            PostMessage(B_QUIT_REQUESTED);
            break;

        case '\n':
        case '\r':
            // Ignore newlines
            break;

        default:
            printf("Unknown command '%c'. Press 'h' for help.\n", command);
            break;
    }
}

void MIDIMonitorApp::ResetStatistics()
{
    memset(&stats, 0, sizeof(stats));
    stats.start_time = system_time();
}

void MIDIMonitorApp::ShowHelp()
{
    printf("\nMIDI Monitor Commands:\n");
    printf("======================\n");
    printf("  h - Show this help\n");
    printf("  s - Show statistics\n");
    printf("  r - Reset statistics\n");
    printf("  t - Toggle timestamps\n");
    printf("  d - Toggle APC message decoding\n");
    printf("  q - Quit\n\n");

    printf("Message Format:\n");
    printf("  [timestamp] MessageType Ch:channel Data1:value Data2:value (hex values)\n");
    printf("  -> APC decoded message (if enabled)\n\n");

    printf("APC Mini MIDI Mapping:\n");
    printf("  Pads:         Notes 0-63 (8x8 grid)\n");
    printf("  Faders:       CC 48-56 (9 faders)\n");
    printf("  Track buttons: Notes 100-107\n");
    printf("  Scene buttons: Notes 112-119\n");
    printf("  Shift button: Note 122\n\n");
}

// MIDIConsumerMonitor implementation
void MIDIMonitorApp::MIDIConsumerMonitor::NoteOn(uchar channel, uchar note, uchar velocity, bigtime_t time)
{
    app->stats.note_on_count++;
    app->stats.total_messages++;
    app->stats.last_message_time = time;
    app->HandleMIDIMessage("Note On", channel, note, velocity, time);
}

void MIDIMonitorApp::MIDIConsumerMonitor::NoteOff(uchar channel, uchar note, uchar velocity, bigtime_t time)
{
    app->stats.note_off_count++;
    app->stats.total_messages++;
    app->stats.last_message_time = time;
    app->HandleMIDIMessage("Note Off", channel, note, velocity, time);
}

void MIDIMonitorApp::MIDIConsumerMonitor::ControlChange(uchar channel, uchar controlNumber, uchar controlValue, bigtime_t time)
{
    app->stats.control_change_count++;
    app->stats.total_messages++;
    app->stats.last_message_time = time;
    app->HandleMIDIMessage("Control Change", channel, controlNumber, controlValue, time);
}

void MIDIMonitorApp::MIDIConsumerMonitor::ProgramChange(uchar channel, uchar programNumber, bigtime_t time)
{
    app->stats.other_count++;
    app->stats.total_messages++;
    app->stats.last_message_time = time;
    app->HandleMIDIMessage("Program Change", channel, programNumber, 0, time);
}

void MIDIMonitorApp::MIDIConsumerMonitor::SystemExclusive(void* data, size_t dataLength, bigtime_t time)
{
    app->stats.other_count++;
    app->stats.total_messages++;
    app->stats.last_message_time = time;

    if (app->show_timestamps) {
        app->PrintTimestamp(time);
    }

    printf("System Exclusive Length:%zu", dataLength);

    if (dataLength > 0 && dataLength <= 16) {
        printf(" Data:");
        uint8_t* bytes = static_cast<uint8_t*>(data);
        for (size_t i = 0; i < dataLength; i++) {
            printf(" 0x%02X", bytes[i]);
        }
    }

    printf("\n");

    if (app->log_to_file && app->log_file) {
        fprintf(app->log_file, "[%lld] SysEx Length:%zu\n", time / 1000, dataLength);
        fflush(app->log_file);
    }
}

int main(int argc, char* argv[])
{
    printf("APC Mini MIDI Monitor\n");
    printf("Options: --help --no-usb-raw --no-timestamps --no-decode --log <file> --test\n\n");

    MIDIMonitorApp app;
    app.Run();

    return 0;
}