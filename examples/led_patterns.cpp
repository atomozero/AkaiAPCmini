#include <Application.h>
#include <OS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "../src/usb_raw_midi.h"
#include "../src/apc_mini_defs.h"

class LEDPatternsApp : public BApplication {
public:
    LEDPatternsApp();
    virtual ~LEDPatternsApp();

    virtual void ReadyToRun() override;
    virtual bool QuitRequested() override;

    void RunDemo();

private:
    USBRawMIDI* usb_midi;
    bool running;
    bool simulation_mode;

    // Pattern functions
    void PatternAllOff();
    void PatternAllOn(APCMiniLEDColor color);
    void PatternChaser(APCMiniLEDColor color, int delay_ms);
    void PatternSpiral(APCMiniLEDColor color, int delay_ms);
    void PatternRandom(int duration_ms);
    void PatternRainbow(int cycles);
    void PatternBlink(APCMiniLEDColor color, int count);
    void PatternWave(APCMiniLEDColor color, int cycles);
    void PatternCheckerboard(APCMiniLEDColor color1, APCMiniLEDColor color2);
    void PatternBorders(APCMiniLEDColor color);
    void PatternCross(APCMiniLEDColor color);
    void PatternDiagonal(APCMiniLEDColor color);

    // Utility functions
    void SetPadColor(int x, int y, APCMiniLEDColor color);
    void SetAllPads(APCMiniLEDColor color);
    bool IsValidPosition(int x, int y);
    void ShowPattern(const char* name);
    void WaitForUser();

    bool InitializeConnection();
};

// Global application instance for signal handling
LEDPatternsApp* g_app = nullptr;

void signal_handler(int sig)
{
    if (g_app) {
        printf("\nShutting down LED patterns demo...\n");
        g_app->PostMessage(B_QUIT_REQUESTED);
    }
}

LEDPatternsApp::LEDPatternsApp()
    : BApplication("application/x-vnd.apc-mini-led-patterns")
    , usb_midi(nullptr)
    , running(true)
    , simulation_mode(false)
{
    g_app = this;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

LEDPatternsApp::~LEDPatternsApp()
{
    if (usb_midi) {
        usb_midi->Shutdown();
        delete usb_midi;
    }
    g_app = nullptr;
}

void LEDPatternsApp::ReadyToRun()
{
    printf("APC Mini LED Patterns Demo\n");
    printf("==========================\n\n");

    // Check command line arguments
    int32 argc;
    char** argv;
    GetArgvReceived(&argc, &argv);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--simulation") == 0 || strcmp(argv[i], "--demo") == 0) {
            simulation_mode = true;
            printf("Running in simulation mode (no hardware required)\n");
        }
    }

    if (!simulation_mode && !InitializeConnection()) {
        printf("Hardware not available, switching to simulation mode\n");
        simulation_mode = true;
    }

    RunDemo();
    PostMessage(B_QUIT_REQUESTED);
}

bool LEDPatternsApp::QuitRequested()
{
    running = false;
    return true;
}

bool LEDPatternsApp::InitializeConnection()
{
    usb_midi = new USBRawMIDI();

    APCMiniError result = usb_midi->Initialize();
    if (result != APC_SUCCESS) {
        printf("Failed to initialize USB connection: %d\n", result);
        delete usb_midi;
        usb_midi = nullptr;
        return false;
    }

    printf("Connected to APC Mini via USB Raw\n");
    return true;
}

void LEDPatternsApp::RunDemo()
{
    printf("\nStarting LED patterns demonstration...\n");
    printf("Press Ctrl+C to stop at any time\n\n");

    if (simulation_mode) {
        printf("SIMULATION MODE: LED commands will be printed to console\n\n");
    }

    // Turn off all LEDs first
    ShowPattern("All Off");
    PatternAllOff();
    snooze(1000000); // 1 second

    if (!running) return;

    // Pattern 1: All colors
    ShowPattern("Solid Colors");
    APCMiniLEDColor colors[] = {
        APC_LED_GREEN, APC_LED_RED, APC_LED_YELLOW
    };
    const char* color_names[] = { "Green", "Red", "Yellow" };

    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]) && running; i++) {
        printf("  %s...\n", color_names[i]);
        PatternAllOn(colors[i]);
        snooze(1500000); // 1.5 seconds
    }

    if (!running) return;

    // Pattern 2: Blinking colors
    ShowPattern("Blinking Colors");
    APCMiniLEDColor blink_colors[] = {
        APC_LED_GREEN_BLINK, APC_LED_RED_BLINK, APC_LED_YELLOW_BLINK
    };
    const char* blink_names[] = { "Green Blink", "Red Blink", "Yellow Blink" };

    for (size_t i = 0; i < sizeof(blink_colors) / sizeof(blink_colors[0]) && running; i++) {
        printf("  %s...\n", blink_names[i]);
        PatternAllOn(blink_colors[i]);
        snooze(2000000); // 2 seconds
    }

    PatternAllOff();
    if (!running) return;

    // Pattern 3: Chaser
    ShowPattern("Chaser");
    PatternChaser(APC_LED_GREEN, 100);
    if (!running) return;

    // Pattern 4: Spiral
    ShowPattern("Spiral");
    PatternSpiral(APC_LED_RED, 150);
    if (!running) return;

    // Pattern 5: Random
    ShowPattern("Random Lights");
    PatternRandom(5000);
    if (!running) return;

    // Pattern 6: Rainbow
    ShowPattern("Rainbow Cycle");
    PatternRainbow(3);
    if (!running) return;

    // Pattern 7: Wave
    ShowPattern("Wave Effect");
    PatternWave(APC_LED_YELLOW, 2);
    if (!running) return;

    // Pattern 8: Checkerboard
    ShowPattern("Checkerboard");
    PatternCheckerboard(APC_LED_GREEN, APC_LED_RED);
    snooze(2000000);
    if (!running) return;

    // Pattern 9: Borders
    ShowPattern("Border Effect");
    PatternBorders(APC_LED_YELLOW);
    snooze(2000000);
    if (!running) return;

    // Pattern 10: Cross
    ShowPattern("Cross Pattern");
    PatternCross(APC_LED_GREEN);
    snooze(2000000);
    if (!running) return;

    // Pattern 11: Diagonal
    ShowPattern("Diagonal Lines");
    PatternDiagonal(APC_LED_RED);
    snooze(2000000);
    if (!running) return;

    // Pattern 12: Final blink sequence
    ShowPattern("Finale");
    PatternBlink(APC_LED_GREEN, 3);
    PatternBlink(APC_LED_RED, 3);
    PatternBlink(APC_LED_YELLOW, 3);

    // Turn everything off
    PatternAllOff();

    printf("\nLED patterns demonstration completed!\n");
}

void LEDPatternsApp::PatternAllOff()
{
    SetAllPads(APC_LED_OFF);
}

void LEDPatternsApp::PatternAllOn(APCMiniLEDColor color)
{
    SetAllPads(color);
}

void LEDPatternsApp::PatternChaser(APCMiniLEDColor color, int delay_ms)
{
    // Chase around the perimeter
    int positions[][2] = {
        // Top row
        {0,0}, {1,0}, {2,0}, {3,0}, {4,0}, {5,0}, {6,0}, {7,0},
        // Right column
        {7,1}, {7,2}, {7,3}, {7,4}, {7,5}, {7,6}, {7,7},
        // Bottom row (right to left)
        {6,7}, {5,7}, {4,7}, {3,7}, {2,7}, {1,7}, {0,7},
        // Left column (bottom to top)
        {0,6}, {0,5}, {0,4}, {0,3}, {0,2}, {0,1}
    };

    int num_positions = sizeof(positions) / sizeof(positions[0]);

    for (int cycle = 0; cycle < 2 && running; cycle++) {
        for (int i = 0; i < num_positions && running; i++) {
            // Turn off previous LED
            if (i > 0) {
                SetPadColor(positions[i-1][0], positions[i-1][1], APC_LED_OFF);
            } else if (cycle > 0) {
                SetPadColor(positions[num_positions-1][0], positions[num_positions-1][1], APC_LED_OFF);
            }

            // Turn on current LED
            SetPadColor(positions[i][0], positions[i][1], color);
            snooze(delay_ms * 1000);
        }
    }

    // Turn off the last LED
    SetPadColor(positions[num_positions-1][0], positions[num_positions-1][1], APC_LED_OFF);
}

void LEDPatternsApp::PatternSpiral(APCMiniLEDColor color, int delay_ms)
{
    // Spiral from outside to inside
    PatternAllOff();

    // Define spiral path
    int spiral[][2] = {
        // Outer ring
        {0,0}, {1,0}, {2,0}, {3,0}, {4,0}, {5,0}, {6,0}, {7,0},
        {7,1}, {7,2}, {7,3}, {7,4}, {7,5}, {7,6}, {7,7},
        {6,7}, {5,7}, {4,7}, {3,7}, {2,7}, {1,7}, {0,7},
        {0,6}, {0,5}, {0,4}, {0,3}, {0,2}, {0,1},
        // Second ring
        {1,1}, {2,1}, {3,1}, {4,1}, {5,1}, {6,1},
        {6,2}, {6,3}, {6,4}, {6,5}, {6,6},
        {5,6}, {4,6}, {3,6}, {2,6}, {1,6},
        {1,5}, {1,4}, {1,3}, {1,2},
        // Third ring
        {2,2}, {3,2}, {4,2}, {5,2},
        {5,3}, {5,4}, {5,5},
        {4,5}, {3,5}, {2,5},
        {2,4}, {2,3},
        // Center
        {3,3}, {4,3}, {4,4}, {3,4}
    };

    int num_positions = sizeof(spiral) / sizeof(spiral[0]);

    for (int i = 0; i < num_positions && running; i++) {
        SetPadColor(spiral[i][0], spiral[i][1], color);
        snooze(delay_ms * 1000);
    }

    snooze(1000000); // Hold final pattern for 1 second
}

void LEDPatternsApp::PatternRandom(int duration_ms)
{
    PatternAllOff();

    bigtime_t start_time = system_time();
    bigtime_t duration_us = duration_ms * 1000;

    while ((system_time() - start_time) < duration_us && running) {
        // Pick random position and color
        int x = rand() % APC_MINI_PAD_COLS;
        int y = rand() % APC_MINI_PAD_ROWS;

        APCMiniLEDColor colors[] = {
            APC_LED_GREEN, APC_LED_RED, APC_LED_YELLOW,
            APC_LED_GREEN_BLINK, APC_LED_RED_BLINK, APC_LED_YELLOW_BLINK,
            APC_LED_OFF
        };

        APCMiniLEDColor color = colors[rand() % (sizeof(colors) / sizeof(colors[0]))];
        SetPadColor(x, y, color);

        snooze(100000); // 100ms between changes
    }
}

void LEDPatternsApp::PatternRainbow(int cycles)
{
    for (int cycle = 0; cycle < cycles && running; cycle++) {
        // Cycle through colors in waves
        for (int phase = 0; phase < 8 && running; phase++) {
            for (int x = 0; x < APC_MINI_PAD_COLS; x++) {
                for (int y = 0; y < APC_MINI_PAD_ROWS; y++) {
                    // Create a wave pattern based on distance from corner
                    int distance = (x + y + phase) % 3;
                    APCMiniLEDColor color;

                    switch (distance) {
                        case 0: color = APC_LED_GREEN; break;
                        case 1: color = APC_LED_RED; break;
                        case 2: color = APC_LED_YELLOW; break;
                        default: color = APC_LED_OFF; break;
                    }

                    SetPadColor(x, y, color);
                }
            }
            snooze(300000); // 300ms per phase
        }
    }
}

void LEDPatternsApp::PatternBlink(APCMiniLEDColor color, int count)
{
    for (int i = 0; i < count && running; i++) {
        SetAllPads(color);
        snooze(300000); // 300ms on
        SetAllPads(APC_LED_OFF);
        snooze(300000); // 300ms off
    }
}

void LEDPatternsApp::PatternWave(APCMiniLEDColor color, int cycles)
{
    for (int cycle = 0; cycle < cycles && running; cycle++) {
        // Horizontal wave
        for (int x = 0; x < APC_MINI_PAD_COLS && running; x++) {
            // Clear previous column
            if (x > 0) {
                for (int y = 0; y < APC_MINI_PAD_ROWS; y++) {
                    SetPadColor(x-1, y, APC_LED_OFF);
                }
            }

            // Light up current column
            for (int y = 0; y < APC_MINI_PAD_ROWS; y++) {
                SetPadColor(x, y, color);
            }
            snooze(200000); // 200ms
        }

        // Clear last column
        for (int y = 0; y < APC_MINI_PAD_ROWS; y++) {
            SetPadColor(APC_MINI_PAD_COLS-1, y, APC_LED_OFF);
        }

        // Vertical wave
        for (int y = 0; y < APC_MINI_PAD_ROWS && running; y++) {
            // Clear previous row
            if (y > 0) {
                for (int x = 0; x < APC_MINI_PAD_COLS; x++) {
                    SetPadColor(x, y-1, APC_LED_OFF);
                }
            }

            // Light up current row
            for (int x = 0; x < APC_MINI_PAD_COLS; x++) {
                SetPadColor(x, y, color);
            }
            snooze(200000); // 200ms
        }

        // Clear last row
        for (int x = 0; x < APC_MINI_PAD_COLS; x++) {
            SetPadColor(x, APC_MINI_PAD_ROWS-1, APC_LED_OFF);
        }
    }
}

void LEDPatternsApp::PatternCheckerboard(APCMiniLEDColor color1, APCMiniLEDColor color2)
{
    for (int x = 0; x < APC_MINI_PAD_COLS; x++) {
        for (int y = 0; y < APC_MINI_PAD_ROWS; y++) {
            APCMiniLEDColor color = ((x + y) % 2 == 0) ? color1 : color2;
            SetPadColor(x, y, color);
        }
    }
}

void LEDPatternsApp::PatternBorders(APCMiniLEDColor color)
{
    PatternAllOff();

    // Light up border pads
    for (int x = 0; x < APC_MINI_PAD_COLS; x++) {
        SetPadColor(x, 0, color); // Top row
        SetPadColor(x, APC_MINI_PAD_ROWS-1, color); // Bottom row
    }

    for (int y = 0; y < APC_MINI_PAD_ROWS; y++) {
        SetPadColor(0, y, color); // Left column
        SetPadColor(APC_MINI_PAD_COLS-1, y, color); // Right column
    }
}

void LEDPatternsApp::PatternCross(APCMiniLEDColor color)
{
    PatternAllOff();

    int center_x = APC_MINI_PAD_COLS / 2;
    int center_y = APC_MINI_PAD_ROWS / 2;

    // Vertical line
    for (int y = 0; y < APC_MINI_PAD_ROWS; y++) {
        SetPadColor(center_x, y, color);
        SetPadColor(center_x - 1, y, color);
    }

    // Horizontal line
    for (int x = 0; x < APC_MINI_PAD_COLS; x++) {
        SetPadColor(x, center_y, color);
        SetPadColor(x, center_y - 1, color);
    }
}

void LEDPatternsApp::PatternDiagonal(APCMiniLEDColor color)
{
    PatternAllOff();

    // Main diagonal (top-left to bottom-right)
    for (int i = 0; i < APC_MINI_PAD_COLS && i < APC_MINI_PAD_ROWS; i++) {
        SetPadColor(i, i, color);
    }

    // Anti-diagonal (top-right to bottom-left)
    for (int i = 0; i < APC_MINI_PAD_COLS; i++) {
        int y = APC_MINI_PAD_ROWS - 1 - i;
        if (y >= 0) {
            SetPadColor(i, y, color);
        }
    }
}

void LEDPatternsApp::SetPadColor(int x, int y, APCMiniLEDColor color)
{
    if (!IsValidPosition(x, y)) {
        return;
    }

    if (simulation_mode) {
        printf("LED[%d,%d] = %s\n", x, y,
               (color == APC_LED_OFF) ? "OFF" :
               (color == APC_LED_GREEN) ? "GREEN" :
               (color == APC_LED_GREEN_BLINK) ? "GREEN_BLINK" :
               (color == APC_LED_RED) ? "RED" :
               (color == APC_LED_RED_BLINK) ? "RED_BLINK" :
               (color == APC_LED_YELLOW) ? "YELLOW" :
               (color == APC_LED_YELLOW_BLINK) ? "YELLOW_BLINK" : "UNKNOWN");
    } else if (usb_midi) {
        uint8_t pad = PAD_XY_TO_NOTE(x, y);
        usb_midi->SetPadColor(pad, color);
    }
}

void LEDPatternsApp::SetAllPads(APCMiniLEDColor color)
{
    for (int x = 0; x < APC_MINI_PAD_COLS; x++) {
        for (int y = 0; y < APC_MINI_PAD_ROWS; y++) {
            SetPadColor(x, y, color);
        }
    }
}

bool LEDPatternsApp::IsValidPosition(int x, int y)
{
    return (x >= 0 && x < APC_MINI_PAD_COLS && y >= 0 && y < APC_MINI_PAD_ROWS);
}

void LEDPatternsApp::ShowPattern(const char* name)
{
    printf("Pattern: %s\n", name);
}

int main(int argc, char* argv[])
{
    printf("APC Mini LED Patterns Demo\n");
    printf("Use --simulation or --demo for simulation mode\n\n");

    LEDPatternsApp app;
    app.Run();

    return 0;
}