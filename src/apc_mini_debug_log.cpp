#include "apc_mini_gui.h"
#include <ScrollView.h>
#include <TextView.h>
#include <Button.h>
#include <StringView.h>
#include <Font.h>
#include <time.h>
#include <stdio.h>

// Debug Log Window Implementation
DebugLogWindow::DebugLogWindow()
    : BWindow(BRect(50, 50, 650, 500), "APC Mini Debug Log", B_TITLED_WINDOW,
              B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
    , scroll_view(nullptr)
    , log_text(nullptr)
    , clear_button(nullptr)
    , status_label(nullptr)
    , current_lines(0)
{
    InitializeInterface();
}

DebugLogWindow::~DebugLogWindow()
{
}

void DebugLogWindow::InitializeInterface()
{
    // SetViewColor(240, 240, 240); // BWindow doesn't have SetViewColor

    // Status label at top
    BRect status_rect = Bounds();
    status_rect.bottom = status_rect.top + 25;
    status_label = new BStringView(status_rect, "status", "MIDI Debug Log - Ready");
    status_label->SetAlignment(B_ALIGN_CENTER);
    status_label->SetFont(be_bold_font);
    AddChild(status_label);

    // Clear button
    BRect button_rect = Bounds();
    button_rect.top = status_rect.bottom + 5;
    button_rect.bottom = button_rect.top + 25;
    button_rect.left = button_rect.right - 80;
    clear_button = new BButton(button_rect, "clear", "Clear", new BMessage('clr'));
    AddChild(clear_button);

    // Text view for log content
    BRect text_rect = Bounds();
    text_rect.top = button_rect.bottom + 5;
    text_rect.bottom -= 15;
    text_rect.right -= B_V_SCROLL_BAR_WIDTH;

    log_text = new BTextView(text_rect, "log_text", text_rect.OffsetToCopy(0, 0),
                            B_FOLLOW_ALL, B_WILL_DRAW);

    // Use simpler colors first - white text on dark background
    log_text->SetViewColor(32, 32, 32);    // Dark gray background
    log_text->SetHighColor(255, 255, 255); // White text
    log_text->SetLowColor(32, 32, 32);     // Dark gray
    log_text->MakeEditable(false);
    log_text->SetWordWrap(true);           // Enable word wrap for now

    // Use monospace font for better alignment
    BFont mono_font(be_fixed_font);
    mono_font.SetSize(12);
    log_text->SetFont(&mono_font);

    // Add some initial text to test
    log_text->SetText("Debug Log Window - Ready\nMove faders to see MIDI messages here...\n");

    // Scroll view
    scroll_view = new BScrollView("scroll", log_text, B_FOLLOW_ALL, 0, false, true);
    AddChild(scroll_view);

    // Initial log message
    LogStatusMessage("Debug log window initialized");
}

void DebugLogWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case 'clr':
            ClearLog();
            break;
        default:
            BWindow::MessageReceived(message);
            break;
    }
}

bool DebugLogWindow::QuitRequested()
{
    Hide();
    return false; // Don't actually quit, just hide
}

void DebugLogWindow::LogMIDIMessage(const char* direction, uint8_t status, uint8_t data1, uint8_t data2)
{
    char timestamp[32];
    time_t now = time(nullptr);
    struct tm* tm_now = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_now);

    char log_line[256];
    snprintf(log_line, sizeof(log_line),
             "[%s] %s MIDI: Status=0x%02X Data1=0x%02X Data2=0x%02X (%s)\n",
             timestamp, direction, status, data1, data2,
             (status & 0xF0) == 0x90 ? "Note On" :
             (status & 0xF0) == 0x80 ? "Note Off" :
             (status & 0xF0) == 0xB0 ? "Control Change" :
             (status & 0xF0) == 0xF0 ? "System" : "Other");

    AppendLogLine(log_line);
}

void DebugLogWindow::LogRawData(const char* direction, const uint8_t* data, size_t length)
{
    char timestamp[32];
    time_t now = time(nullptr);
    struct tm* tm_now = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_now);

    char log_line[512];
    char hex_data[256] = "";

    for (size_t i = 0; i < length && i < 32; i++) {
        char byte_str[8];
        snprintf(byte_str, sizeof(byte_str), "%02X ", data[i]);
        strcat(hex_data, byte_str);
    }

    snprintf(log_line, sizeof(log_line),
             "[%s] %s RAW (%zu bytes): %s\n",
             timestamp, direction, length, hex_data);

    AppendLogLine(log_line);
}

void DebugLogWindow::LogStatusMessage(const char* message)
{
    char timestamp[32];
    time_t now = time(nullptr);
    struct tm* tm_now = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_now);

    char log_line[512];
    snprintf(log_line, sizeof(log_line),
             "[%s] STATUS: %s\n",
             timestamp, message);

    AppendLogLine(log_line);
}

void DebugLogWindow::ClearLog()
{
    if (log_text && Lock()) {
        log_text->SetText("");
        current_lines = 0;
        Unlock();
    }
    LogStatusMessage("Log cleared");
}

void DebugLogWindow::AppendLogLine(const char* line)
{
    if (!log_text) {
        printf("AppendLogLine: log_text is null!\n");
        return;
    }

    printf("AppendLogLine: %s", line); // Debug output

    if (Lock()) {
        // Limit log size
        if (current_lines >= MAX_LOG_LINES) {
            // Remove first 100 lines to keep log manageable
            int32 line_start = 0;
            for (int i = 0; i < 100; i++) {
                int32 next_line = log_text->OffsetAt(line_start);
                if (next_line < log_text->TextLength()) {
                    line_start = next_line + 1;
                } else {
                    break;
                }
            }
            log_text->Delete(0, line_start);
            current_lines -= 100;
        }

        // Add new line
        log_text->Insert(log_text->TextLength(), line, strlen(line));
        current_lines++;

        // Auto-scroll to bottom
        log_text->ScrollToSelection();

        Unlock();
    }
}