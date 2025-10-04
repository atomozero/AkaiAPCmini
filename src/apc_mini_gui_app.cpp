#include "apc_mini_gui.h"
#include <Roster.h>
#include <Path.h>
#include <Resources.h>
#include <Alert.h>
#include <MessageRunner.h>
#include <stdio.h>

// ===============================
// APCMiniWindow Implementation
// ===============================

APCMiniWindow::APCMiniWindow()
    : BWindow(BRect(100, 100, 800, 600), "APC Mini MK2 Controller",
              B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE)
    , app(nullptr)
    , pad_matrix(nullptr)
    , fader_panel(nullptr)
    , button_panel(nullptr)
    , menu_bar(nullptr)
    , status_view(nullptr)
    , debug_window(nullptr)
    , background_view(nullptr)
    // , main_container(nullptr) // Removed
    , is_connected(false)
{
    // Initialize track buttons array
    for (int i = 0; i < 8; i++) {
        track_buttons[i] = nullptr;
    }

    // Initialize per-fader ignore flags
    for (int i = 0; i < 9; i++) {
        ignore_hardware_updates[i] = false;
        ignore_flag_timestamp[i] = 0;
    }

    InitializeInterface();

    // Debug window will be created on demand when requested

    // Center window on screen
    BScreen screen;
    BRect screen_frame = screen.Frame();
    BRect window_frame = Frame();
    MoveTo((screen_frame.Width() - window_frame.Width()) / 2,
           (screen_frame.Height() - window_frame.Height()) / 2);
}

APCMiniWindow::~APCMiniWindow()
{
}

void APCMiniWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_PAD_PRESSED:
        {
            int32 pad_index;
            bool pressed;
            int32 velocity;

            if (message->FindInt32("pad_index", &pad_index) == B_OK &&
                message->FindBool("pressed", &pressed) == B_OK &&
                message->FindInt32("velocity", &velocity) == B_OK) {

                if (pressed) {
                    UpdatePadPressDirectly(pad_index, velocity);
                } else {
                    UpdatePadReleaseDirectly(pad_index);
                }
            }
            break;
        }

        case MSG_FADER_CHANGED:
        {
            int32 fader_index;
            int32 value;

            if (message->FindInt32("fader_index", &fader_index) == B_OK &&
                message->FindInt32("value", &value) == B_OK) {

                // This is a GUI-initiated change, send to hardware
                // printf("GUI fader %d changed to %d - sending to hardware\n", (int)fader_index, (int)value); // Disabled for performance

                // Set per-fader flag to ignore feedback from hardware for a short time
                uint8_t fader_idx = (fader_index < APC_MINI_TRACK_FADER_COUNT) ? fader_index : APC_MINI_TRACK_FADER_COUNT;
                ignore_hardware_updates[fader_idx] = true;
                ignore_flag_timestamp[fader_idx] = system_time();

                // Send to hardware
                if (app) {
                    uint8_t cc_number;
                    if (fader_index < APC_MINI_TRACK_FADER_COUNT) {
                        cc_number = APC_MINI_FADER_CC_START + fader_index;
                    } else {
                        cc_number = APC_MINI_MASTER_CC;
                    }
                    static_cast<APCMiniGUIApp*>(app)->SendControlChange(cc_number, value);
                }

                // Clear per-fader flag after ultra-minimal delay (aggressive low latency)
                BMessage* clear_flag = new BMessage(MSG_HARDWARE_FADER_CHANGE);
                clear_flag->AddBool("clear_flag", true);
                clear_flag->AddInt32("fader_index", fader_idx); // Include which fader to clear
                // Ultra-low latency: minimal delay for immediate responsiveness
                if (BMessageRunner::StartSending(this, clear_flag, 20000, 1) != B_OK) {
                    // If BMessageRunner fails, clear flag immediately to prevent permanent blocking
                    ignore_hardware_updates[fader_idx] = false;
                    ignore_flag_timestamp[fader_idx] = 0;
                    printf("Warning: BMessageRunner failed, clearing ignore flag immediately\n");
                }
            }
            break;
        }

        case MSG_TRACK_BUTTON:
        {
            int32 button_index;
            bool pressed;

            if (message->FindInt32("button_index", &button_index) == B_OK &&
                message->FindBool("pressed", &pressed) == B_OK) {

                UpdateTrackButtonDirectly(button_index, pressed);
            }
            break;
        }

        case MSG_SCENE_BUTTON:
        {
            int32 button_index;
            bool pressed;

            if (message->FindInt32("button_index", &button_index) == B_OK &&
                message->FindBool("pressed", &pressed) == B_OK) {

                UpdateSceneButtonDirectly(button_index, pressed);
            }
            break;
        }

        case MSG_SHIFT_BUTTON:
        {
            bool pressed;
            if (message->FindBool("pressed", &pressed) == B_OK) {
                UpdateShiftButtonDirectly(pressed);
            }
            break;
        }

        case MSG_MENU_TOGGLE_USB:
            HandleMenuSelection(message);
            break;

        case MSG_MENU_RESET_DEVICE:
            HandleMenuSelection(message);
            break;

        case MSG_MENU_TEST_LEDS:
            HandleMenuSelection(message);
            break;

        case MSG_MENU_ABOUT:
            HandleMenuSelection(message);
            break;

        case MSG_MENU_DEBUG_LOG:
            HandleMenuSelection(message);
            break;

        case MSG_MENU_HAIKU_LOGO:
            // Launch Haiku logo in background thread to keep GUI responsive
            {
                thread_id logo_thread = spawn_thread(DrawHaikuLogoThreadEntry, "haiku_logo_thread",
                                                    B_NORMAL_PRIORITY, this);
                if (logo_thread >= 0) {
                    resume_thread(logo_thread);
                }
            }
            break;

        case MSG_HARDWARE_FADER_CHANGE:
        {
            bool clear_flag;
            if (message->FindBool("clear_flag", &clear_flag) == B_OK && clear_flag) {
                // This is the delayed message to clear the per-fader ignore flag
                int32 clear_fader_index;
                if (message->FindInt32("fader_index", &clear_fader_index) == B_OK &&
                    clear_fader_index >= 0 && clear_fader_index < 9) {
                    ignore_hardware_updates[clear_fader_index] = false;
                    ignore_flag_timestamp[clear_fader_index] = 0;
                    // printf("Cleared ignore flag for fader %d via timer\n", (int)clear_fader_index); // Disabled for performance
                }
                break;
            }

            int32 fader_index, value;
            if (message->FindInt32("fader_index", &fader_index) == B_OK &&
                message->FindInt32("value", &value) == B_OK) {

                // Check if we should ignore hardware updates for this specific fader (to prevent feedback loops)
                uint8_t fader_idx = (fader_index < APC_MINI_TRACK_FADER_COUNT) ? fader_index : APC_MINI_TRACK_FADER_COUNT;
                if (ignore_hardware_updates[fader_idx]) {
                    // Safety check: if too much time has passed, clear the flag to prevent permanent blocking
                    bigtime_t current_time = system_time();
                    if (current_time - ignore_flag_timestamp[fader_idx] > 50000) { // 50ms safety timeout
                        // Silently clear stuck ignore flag to prevent permanent blocking
                        ignore_hardware_updates[fader_idx] = false;
                        ignore_flag_timestamp[fader_idx] = 0;
                    } else {
                        // printf("Ignoring hardware fader %d update (preventing feedback loop)\n", (int)fader_index); // Disabled for performance
                        break;
                    }
                }

                // Update GUI fader to match hardware fader position (safe from main thread)
                if (fader_panel) {
                    // Save the current value to maintain position
                    fader_panel->SetFaderValue(fader_index, value);

                    // Store the current position for persistence and ensure consistency
                    // Performance optimization: debug output disabled in hot path
                    // if (fader_index < APC_MINI_TRACK_FADER_COUNT) {
                    //     printf("Stored track fader %d position: %d\n", (int)fader_index, (int)value);
                    // } else {
                    //     printf("Stored master fader position: %d\n", (int)value);
                    // }

                    // Also send to MIDI output for Patchbay consistency (without causing loop)
                    if (app && !ignore_hardware_updates[fader_idx]) {
                        uint8_t cc_number;
                        if (fader_index < APC_MINI_TRACK_FADER_COUNT) {
                            cc_number = APC_MINI_FADER_CC_START + fader_index;
                        } else {
                            cc_number = APC_MINI_MASTER_CC;
                        }
                        // printf("Forwarding hardware fader %d change to MIDI output (CC %d = %d)\n",
                        //        (int)fader_index, (int)cc_number, (int)value); // Disabled for performance
                        static_cast<APCMiniGUIApp*>(app)->SendControlChange(cc_number, value);
                    }

                    // Update GUI to show new position (optimized for performance)
                    fader_panel->Invalidate();
                }
            }
            break;
        }

        case MSG_HARDWARE_MIDI_EVENT:
        {
            uint8_t status, data1, data2;
            if (message->FindUInt8("status", &status) == B_OK &&
                message->FindUInt8("data1", &data1) == B_OK &&
                message->FindUInt8("data2", &data2) == B_OK && app) {
                static_cast<APCMiniGUIApp*>(app)->HandleMIDIMessage(status, data1, data2);
            }
            break;
        }

        default:
            BWindow::MessageReceived(message);
            break;
    }
}

bool APCMiniWindow::QuitRequested()
{
    be_app->PostMessage(B_QUIT_REQUESTED);
    return true;
}

void APCMiniWindow::UpdateFromDevice(const APCMiniState& state)
{
    if (Lock()) {
        // Update pad colors
        for (int i = 0; i < APC_MINI_PAD_COUNT; i++) {
            if (state.is_mk2_device) {
                pad_matrix->SetPadColor(i, state.pad_rgb_colors[i]);
            } else {
                // Convert legacy color to RGB for display
                APCMiniMK2RGB rgb_color = {0, 0, 0};
                switch (state.pad_colors[i]) {
                    case APC_LED_GREEN:
                        rgb_color = {0, 127, 0};
                        break;
                    case APC_LED_RED:
                        rgb_color = {127, 0, 0};
                        break;
                    case APC_LED_YELLOW:
                        rgb_color = {127, 127, 0};
                        break;
                    default:
                        rgb_color = {0, 0, 0};
                        break;
                }
                pad_matrix->SetPadColor(i, rgb_color);
            }

            pad_matrix->SetPadPressed(i, state.pads[i], state.pad_velocities[i]);
        }

        // Update faders
        for (int i = 0; i < APC_MINI_TRACK_FADER_COUNT; i++) {
            fader_panel->SetFaderValue(i, state.track_fader_values[i]);
        }
        fader_panel->SetFaderValue(APC_MINI_TRACK_FADER_COUNT, state.master_fader_value);

        // Update buttons
        for (int i = 0; i < 8; i++) {
            // Update individual track buttons
            if (track_buttons[i]) {
                track_buttons[i]->SetLEDOn(state.track_buttons[i]);
            }
            // Update scene buttons via button panel
            button_panel->SetSceneButtonLED(i, state.scene_buttons[i]);
        }
        button_panel->SetShiftButtonPressed(state.shift_pressed);

        Unlock();
    }
}

void APCMiniWindow::UpdatePadPressDirectly(uint8_t pad_index, uint8_t velocity)
{
    if (app && Lock()) {
        uint8_t note = APC_MINI_PAD_NOTE_START + pad_index;
        static_cast<APCMiniGUIApp*>(app)->SendNoteOn(note, velocity);

        // Visual feedback - cycle through colors for demo
        static int color_cycle = 0;
        APCMiniMK2RGB demo_colors[] = {
            {127, 0, 0},    // Red
            {0, 127, 0},    // Green
            {0, 0, 127},    // Blue
            {127, 127, 0},  // Yellow
            {127, 0, 127},  // Magenta
            {0, 127, 127},  // Cyan
        };

        SetPadColor(pad_index, demo_colors[color_cycle % 6]);
        color_cycle++;
        Unlock();
    }
}

void APCMiniWindow::HandlePadPress(uint8_t pad_index, uint8_t velocity)
{
    // Try direct update if we're already in the main thread (ultra-low latency path)
    if (find_thread(NULL) == Thread()) {
        // Direct update - no message queue latency
        UpdatePadPressDirectly(pad_index, velocity);
    } else {
        // Send message to main thread to update GUI safely
        BMessage msg(MSG_PAD_PRESSED);
        msg.AddInt32("pad_index", pad_index);
        msg.AddBool("pressed", true);
        msg.AddInt32("velocity", velocity);
        PostMessage(&msg);
    }
}

void APCMiniWindow::UpdatePadReleaseDirectly(uint8_t pad_index)
{
    if (app && Lock()) {
        uint8_t note = APC_MINI_PAD_NOTE_START + pad_index;
        static_cast<APCMiniGUIApp*>(app)->SendNoteOff(note);
        Unlock();
    }
}

void APCMiniWindow::HandlePadRelease(uint8_t pad_index)
{
    // Try direct update if we're already in the main thread (ultra-low latency path)
    if (find_thread(NULL) == Thread()) {
        // Direct update - no message queue latency
        UpdatePadReleaseDirectly(pad_index);
    } else {
        // Send message to main thread to update GUI safely
        BMessage msg(MSG_PAD_PRESSED);
        msg.AddInt32("pad_index", pad_index);
        msg.AddBool("pressed", false);
        msg.AddInt32("velocity", 0);
        PostMessage(&msg);
    }
}

void APCMiniWindow::HandleFaderChange(uint8_t fader_index, uint8_t value)
{
    // Try direct update if we're already in the main thread (ultra-low latency path)
    if (find_thread(NULL) == Thread()) {
        // Direct update - no message queue latency
        UpdateFaderDirectly(fader_index, value);
    } else {
        // Send message to main thread to update GUI safely
        BMessage msg(MSG_HARDWARE_FADER_CHANGE);
        msg.AddInt32("fader_index", fader_index);
        msg.AddInt32("value", value);
        PostMessage(&msg);
    }
}

void APCMiniWindow::UpdateFaderDirectly(uint8_t fader_index, uint8_t value)
{
    // Direct fader update - bypasses message queue for ultra-low latency
    uint8_t fader_idx = (fader_index < APC_MINI_TRACK_FADER_COUNT) ? fader_index : APC_MINI_TRACK_FADER_COUNT;
    if (ignore_hardware_updates[fader_idx]) {
        bigtime_t current_time = system_time();
        if (current_time - ignore_flag_timestamp[fader_idx] > 50000) { // 50ms safety timeout
            ignore_hardware_updates[fader_idx] = false;
            ignore_flag_timestamp[fader_idx] = 0;
        } else {
            return; // Still ignoring updates for this specific fader
        }
    }

    if (fader_panel && Lock()) {
        fader_panel->SetFaderValue(fader_index, value);
        fader_panel->Invalidate();
        Unlock();
    }
}

void APCMiniWindow::UpdateTrackButtonDirectly(uint8_t button_index, bool pressed)
{
    if (app && button_index < 8 && Lock()) {
        uint8_t note = APC_MINI_TRACK_NOTE_START + button_index;
        if (pressed) {
            static_cast<APCMiniGUIApp*>(app)->SendNoteOn(note, 127);
            // Toggle LED state for individual track button
            if (track_buttons[button_index]) {
                track_buttons[button_index]->SetLEDOn(!track_buttons[button_index]->IsPressed());
            }
        } else {
            static_cast<APCMiniGUIApp*>(app)->SendNoteOff(note);
        }
        Unlock();
    }
}

void APCMiniWindow::HandleTrackButton(uint8_t button_index, bool pressed)
{
    // Try direct update if we're already in the main thread (ultra-low latency path)
    if (find_thread(NULL) == Thread()) {
        // Direct update - no message queue latency
        UpdateTrackButtonDirectly(button_index, pressed);
    } else {
        // Send message to main thread to update GUI safely
        BMessage msg(MSG_TRACK_BUTTON);
        msg.AddInt32("button_index", button_index);
        msg.AddBool("pressed", pressed);
        PostMessage(&msg);
    }
}

void APCMiniWindow::UpdateSceneButtonDirectly(uint8_t button_index, bool pressed)
{
    if (app && button_index < 8 && Lock()) {
        uint8_t note = APC_MINI_SCENE_NOTE_START + button_index;
        if (pressed) {
            static_cast<APCMiniGUIApp*>(app)->SendNoteOn(note, 127);
            // Toggle LED state
            button_panel->SetSceneButtonLED(button_index, !button_panel->ChildAt(button_index + 8));
        } else {
            static_cast<APCMiniGUIApp*>(app)->SendNoteOff(note);
        }
        Unlock();
    }
}

void APCMiniWindow::HandleSceneButton(uint8_t button_index, bool pressed)
{
    // Try direct update if we're already in the main thread (ultra-low latency path)
    if (find_thread(NULL) == Thread()) {
        // Direct update - no message queue latency
        UpdateSceneButtonDirectly(button_index, pressed);
    } else {
        // Send message to main thread to update GUI safely
        BMessage msg(MSG_SCENE_BUTTON);
        msg.AddInt32("button_index", button_index);
        msg.AddBool("pressed", pressed);
        PostMessage(&msg);
    }
}

void APCMiniWindow::UpdateShiftButtonDirectly(bool pressed)
{
    if (app && Lock()) {
        if (pressed) {
            static_cast<APCMiniGUIApp*>(app)->SendNoteOn(APC_MINI_SHIFT_NOTE, 127);
        } else {
            static_cast<APCMiniGUIApp*>(app)->SendNoteOff(APC_MINI_SHIFT_NOTE);
        }
        Unlock();
    }
}

void APCMiniWindow::HandleShiftButton(bool pressed)
{
    // Try direct update if we're already in the main thread (ultra-low latency path)
    if (find_thread(NULL) == Thread()) {
        // Direct update - no message queue latency
        UpdateShiftButtonDirectly(pressed);
    } else {
        // Send message to main thread to update GUI safely
        BMessage msg(MSG_SHIFT_BUTTON);
        msg.AddBool("pressed", pressed);
        PostMessage(&msg);
    }
}

void APCMiniWindow::SetPadColor(uint8_t pad_index, const APCMiniMK2RGB& color)
{
    if (pad_matrix && pad_index < APC_MINI_PAD_COUNT) {
        if (Lock()) {
            pad_matrix->SetPadColor(pad_index, color);
            Unlock();
        }

        // Send to hardware if connected
        if (app) {
            static_cast<APCMiniGUIApp*>(app)->SendPadRGB(pad_index, color);
        }
    }
}

void APCMiniWindow::SetTrackButtonLED(uint8_t button_index, bool on)
{
    if (button_index < 8 && track_buttons[button_index]) {
        if (Lock()) {
            track_buttons[button_index]->SetLEDOn(on);
            Unlock();
        }

        // Send to hardware
        if (app) {
            static_cast<APCMiniGUIApp*>(app)->SetTrackButtonLED(button_index, on);
        }
    }
}

void APCMiniWindow::SetSceneButtonLED(uint8_t button_index, bool on)
{
    if (button_panel && button_index < 8) {
        if (Lock()) {
            button_panel->SetSceneButtonLED(button_index, on);
            Unlock();
        }

        // Send to hardware
        if (app) {
            static_cast<APCMiniGUIApp*>(app)->SetSceneButtonLED(button_index, on);
        }
    }
}

int32 APCMiniWindow::DrawHaikuLogoThreadEntry(void* data)
{
    APCMiniWindow* window = static_cast<APCMiniWindow*>(data);
    window->DrawHaikuLogoThread();
    return 0;
}

void APCMiniWindow::DrawHaikuLogoThread()
{
    // Create the Haiku OS "H" pattern on the 8x8 LED grid (now in background thread)
    // The grid is arranged as follows (pad indices):
    //  56 57 58 59 60 61 62 63  (row 7)
    //  48 49 50 51 52 53 54 55  (row 6)
    //  40 41 42 43 44 45 46 47  (row 5)
    //  32 33 34 35 36 37 38 39  (row 4)
    //  24 25 26 27 28 29 30 31  (row 3)
    //  16 17 18 19 20 21 22 23  (row 2)
    //   8  9 10 11 12 13 14 15  (row 1)
    //   0  1  2  3  4  5  6  7  (row 0)

    // First, clear all pads to black
    APCMiniMK2RGB black = {0, 0, 0};
    for (int i = 0; i < APC_MINI_PAD_COUNT; i++) {
        SetPadColor(i, black);
        snooze(10000); // Small delay for smooth visual effect (safe in background thread)
    }

    // Brief pause before drawing the H
    snooze(200000); // 200ms

    // Haiku orange color (similar to the official Haiku logo)
    APCMiniMK2RGB orange = {255, 140, 0};  // Haiku orange

    // Draw the "H" pattern
    // Left vertical line (columns 1 and 2)
    int left_column_pads[] = {1, 9, 17, 25, 33, 41, 49, 57};     // column 1
    int left_column2_pads[] = {2, 10, 18, 26, 34, 42, 50, 58};   // column 2

    // Right vertical line (columns 5 and 6)
    int right_column_pads[] = {5, 13, 21, 29, 37, 45, 53, 61};   // column 5
    int right_column2_pads[] = {6, 14, 22, 30, 38, 46, 54, 62};  // column 6

    // Horizontal line in the middle (row 3 and 4)
    int horizontal_pads[] = {25, 26, 27, 28, 29, 30,     // row 3
                            33, 34, 35, 36, 37, 38};     // row 4

    // Set all H pattern pads to orange with animation effect
    for (int i = 0; i < 8; i++) {
        SetPadColor(left_column_pads[i], orange);
        SetPadColor(left_column2_pads[i], orange);
        SetPadColor(right_column_pads[i], orange);
        SetPadColor(right_column2_pads[i], orange);
        snooze(50000); // 50ms delay per row for animation effect
    }

    for (int i = 0; i < 12; i++) {
        SetPadColor(horizontal_pads[i], orange);
        snooze(30000); // 30ms delay for horizontal line animation
    }
}

void APCMiniWindow::DrawHaikuLogo()
{
    // Deprecated - now handled by DrawHaikuLogoThread() for responsiveness
    // This method kept for compatibility but should not be called directly
}

void APCMiniWindow::SetConnectionStatus(bool connected)
{
    is_connected = connected;

    if (status_view && Lock()) {
        BString status_text = "Status: ";
        status_text << (connected ? "Connected" : "Disconnected");
        status_view->SetText(status_text.String());

        // Always use white color for status text
        status_view->SetHighColor(APC_GUI_STATUS_COLOR);  // White status text

        status_view->Invalidate();
        Unlock();
    }
}

void APCMiniWindow::ShowErrorMessage(const char* message)
{
    BAlert* alert = new BAlert("Error", message, "OK", nullptr, nullptr,
                               B_WIDTH_AS_USUAL, B_STOP_ALERT);
    alert->Go();
}

void APCMiniWindow::InitializeInterface()
{
    // Create menu bar
    CreateMenuBar();

    // Create all components first
    CreateComponents();

    // Set up the layout using BLayoutBuilder
    SetupLayout();
}

void APCMiniWindow::CreateMenuBar()
{
    menu_bar = new BMenuBar(Bounds(), "menu_bar");

    // Device menu
    BMenu* device_menu = new BMenu("Device");
    device_menu->AddItem(new BMenuItem("Toggle USB Connection",
                                      new BMessage(MSG_MENU_TOGGLE_USB), 'U'));
    device_menu->AddItem(new BMenuItem("Reset Device",
                                      new BMessage(MSG_MENU_RESET_DEVICE), 'R'));
    device_menu->AddSeparatorItem();
    device_menu->AddItem(new BMenuItem("Test LEDs",
                                      new BMessage(MSG_MENU_TEST_LEDS), 'T'));
    device_menu->AddItem(new BMenuItem("Show Haiku Logo",
                                      new BMessage(MSG_MENU_HAIKU_LOGO), 'H'));

    // Debug menu
    BMenu* debug_menu = new BMenu("Debug");
    debug_menu->AddItem(new BMenuItem("MIDI Log Window",
                                     new BMessage(MSG_MENU_DEBUG_LOG), 'L'));

    // Help menu
    BMenu* help_menu = new BMenu("Help");
    help_menu->AddItem(new BMenuItem("About...",
                                    new BMessage(MSG_MENU_ABOUT)));

    menu_bar->AddItem(device_menu);
    menu_bar->AddItem(debug_menu);
    menu_bar->AddItem(help_menu);

    AddChild(menu_bar);
}

void APCMiniWindow::CreateComponents()
{
    // Components will be created in SetupLayout() with proper positioning
    // This method is now a placeholder for any future initialization
}

void APCMiniWindow::SetupLayout()
{
    // Use manual positioning to ensure proper component visibility and alignment

    float menu_height = menu_bar ? menu_bar->Bounds().Height() : 30;
    float margin = 15;
    float current_y = menu_height + margin;

    // Create branded background view first - exclude menu bar area
    BRect background_rect = Bounds();
    background_rect.top = menu_height;  // Start below the menu bar
    background_view = new BrandedBackgroundView(background_rect);
    AddChild(background_view);

    // Calculate dimensions
    float pad_matrix_width = 8 * APC_GUI_PAD_SIZE + 7 * APC_GUI_PAD_SPACING;
    float pad_matrix_height = 8 * APC_GUI_PAD_SIZE + 7 * APC_GUI_PAD_SPACING;

    // Calculate fader width exactly as CalculateFaderFrame does:
    // 8 track faders: 8 * (width + PAD_SPACING)
    // Master fader: + 12 (spacing) + width
    float fader_width = 8 * (APC_GUI_FADER_WIDTH + APC_GUI_PAD_SPACING) + 12 + APC_GUI_FADER_WIDTH;
    float fader_height = APC_GUI_FADER_HEIGHT + 40;

    // 1. Create and position track buttons (aligned with pad columns)
    for (int i = 0; i < 8; i++) {
        float x = margin + i * (APC_GUI_PAD_SIZE + APC_GUI_PAD_SPACING);
        BRect btn_rect(x, current_y, x + APC_GUI_PAD_SIZE - 1, current_y + APC_GUI_BUTTON_HEIGHT - 1);
        track_buttons[i] = new ControlButton(btn_rect, i, ControlButton::BUTTON_TRACK);
        background_view->AddChild(track_buttons[i]);
    }
    current_y += APC_GUI_BUTTON_HEIGHT + 12;

    // 2. Create and position pad matrix
    BRect pad_rect(margin, current_y, margin + pad_matrix_width - 1, current_y + pad_matrix_height - 1);
    pad_matrix = new PadMatrixView(pad_rect);
    background_view->AddChild(pad_matrix);

    // 3. Create and position scene buttons and shift (to the right of pads)
    float scene_x = margin + pad_matrix_width + 8;
    BRect button_panel_rect(scene_x, current_y, scene_x + APC_GUI_BUTTON_WIDTH + 4 - 1,
                            current_y + pad_matrix_height + 18 + APC_GUI_SHIFT_BUTTON_SIZE - 1);
    button_panel = new ControlButtonView(button_panel_rect);
    background_view->AddChild(button_panel);

    current_y += pad_matrix_height + 62;

    // 4. Create and position faders (below pads, aligned with pad matrix)
    BRect fader_rect(margin, current_y, margin + fader_width - 1, current_y + fader_height - 1);
    fader_panel = new FaderView(fader_rect);
    background_view->AddChild(fader_panel);

    current_y += fader_height + 15;

    // 5. Create and position status bar at the bottom with background panel
    BRect status_panel_rect(margin - 5, current_y - 5, margin + fader_width + 4, current_y + 30);
    BBox* status_panel = new BBox(status_panel_rect, "status_panel");
    status_panel->SetViewColor(rgb_color{45, 44, 43, 255});  // Slightly lighter warm background
    status_panel->SetBorder(B_PLAIN_BORDER);
    background_view->AddChild(status_panel);

    // Status text inside the panel
    BRect status_rect(8, 8, status_panel_rect.Width() - 8, 22);
    status_view = new BStringView(status_rect, "status", "Status: Disconnected");
    status_view->SetHighColor(APC_GUI_STATUS_COLOR);  // White status text
    status_view->SetFontSize(11);  // Slightly larger font
    BFont font;
    status_view->GetFont(&font);
    font.SetFace(B_BOLD_FACE);
    status_view->SetFont(&font);
    status_panel->AddChild(status_view);

    current_y += 40;  // Status panel height + margin

    // Calculate optimal window size - ensure both scene buttons and faders are visible
    float scene_buttons_width = scene_x + APC_GUI_BUTTON_WIDTH + 20;
    float faders_width = margin + fader_width + 10;  // margin + fader panel width + right margin

    float content_width = fmax(scene_buttons_width, faders_width);  // Use the larger of the two
    float content_height = current_y + 10;  // Add final bottom margin

    // Resize window to fit content exactly
    ResizeTo(content_width, content_height);
    SetSizeLimits(content_width - 20, content_width + 50, content_height - 20, content_height + 50);

    // Center window on screen
    BScreen screen;
    BRect screen_frame = screen.Frame();
    float x = (screen_frame.Width() - content_width) / 2;
    float y = (screen_frame.Height() - content_height) / 2;
    MoveTo(x, y);
}

BView* APCMiniWindow::CreateTrackButtonsGroup()
{
    // Create a container view for track buttons
    BView* track_container = new BView("track_buttons_container", 0, nullptr);
    track_container->SetViewColor(APC_GUI_DEVICE_BODY_COLOR);

    // Create track buttons using BLayoutBuilder with proper spacing to align with pad columns
    // NO insets to ensure perfect alignment with pad matrix
    BLayoutBuilder::Group<>(track_container, B_HORIZONTAL, APC_GUI_PAD_SPACING)
        .Add(CreateTrackButton(0))
        .Add(CreateTrackButton(1))
        .Add(CreateTrackButton(2))
        .Add(CreateTrackButton(3))
        .Add(CreateTrackButton(4))
        .Add(CreateTrackButton(5))
        .Add(CreateTrackButton(6))
        .Add(CreateTrackButton(7))
    .End();

    // Set explicit size to match pad matrix width
    float matrix_width = 8 * APC_GUI_PAD_SIZE + 7 * APC_GUI_PAD_SPACING;
    float button_height = APC_GUI_BUTTON_HEIGHT;
    track_container->SetExplicitMinSize(BSize(matrix_width, button_height));
    track_container->SetExplicitMaxSize(BSize(matrix_width, button_height));
    track_container->SetExplicitPreferredSize(BSize(matrix_width, button_height));

    return track_container;
}

ControlButton* APCMiniWindow::CreateTrackButton(uint8_t index)
{
    // Track buttons should be same width as pads for perfect alignment
    BRect button_frame(0, 0, APC_GUI_PAD_SIZE - 1, APC_GUI_BUTTON_HEIGHT - 1);
    ControlButton* button = new ControlButton(button_frame, index, ControlButton::BUTTON_TRACK);

    // Store reference for easy access
    track_buttons[index] = button;

    return button;
}

void APCMiniWindow::CreateBrandingArea()
{
    // Note: Branding will be added through the main layout in a future enhancement
    // For now, the layout system handles all component positioning
}

BRect APCMiniWindow::CalculateMainFrame()
{
    // Calculate the optimal window size based on content
    float pad_area_width = 8 * (APC_GUI_PAD_SIZE + APC_GUI_PAD_SPACING) + 120; // +120 for buttons
    float pad_area_height = 8 * (APC_GUI_PAD_SIZE + APC_GUI_PAD_SPACING);
    float fader_area_height = APC_GUI_FADER_HEIGHT + 30;
    float menu_height = 25;
    float status_height = 25;

    float total_width = pad_area_width + APC_GUI_MARGIN * 2;
    float total_height = pad_area_height + fader_area_height + menu_height + status_height + APC_GUI_MARGIN * 3;

    return BRect(0, 0, total_width, total_height);
}

void APCMiniWindow::HandleMenuSelection(BMessage* message)
{
    switch (message->what) {
        case MSG_MENU_TOGGLE_USB:
            ToggleUSBConnection();
            break;

        case MSG_MENU_RESET_DEVICE:
            ResetDevice();
            break;

        case MSG_MENU_TEST_LEDS:
            TestLEDs();
            break;

        case MSG_MENU_ABOUT:
            ShowAbout();
            break;

        case MSG_MENU_DEBUG_LOG:
            if (!debug_window) {
                printf("Creating debug window on demand...\n");
                debug_window = new DebugLogWindow();
            }

            if (debug_window) {
                printf("Showing debug window\n");
                debug_window->Show();
                debug_window->Activate();
            } else {
                printf("Failed to create or show debug window!\n");
            }
            break;
    }
}

void APCMiniWindow::ToggleUSBConnection()
{
    if (app) {
        APCMiniGUIApp* gui_app = static_cast<APCMiniGUIApp*>(app);
        if (gui_app->IsHardwareConnected()) {
            gui_app->ShutdownHardware();
            SetConnectionStatus(false);
        } else {
            if (gui_app->InitializeHardware()) {
                SetConnectionStatus(true);
            } else {
                ShowErrorMessage("Failed to connect to APC Mini device.\nMake sure the device is connected and try again.");
            }
        }
    }
}

void APCMiniWindow::ResetDevice()
{
    if (app) {
        static_cast<APCMiniGUIApp*>(app)->ResetDeviceState();

        // Reset GUI
        if (pad_matrix) {
            pad_matrix->ResetAllPads();
        }

        if (fader_panel) {
            for (int i = 0; i < 9; i++) {
                fader_panel->SetFaderValue(i, 0);
            }
        }

        // Reset track buttons
        for (int i = 0; i < 8; i++) {
            if (track_buttons[i]) {
                track_buttons[i]->SetLEDOn(false);
            }
        }

        // Reset scene buttons and shift
        if (button_panel) {
            for (int i = 0; i < 8; i++) {
                button_panel->SetSceneButtonLED(i, false);
            }
            button_panel->SetShiftButtonPressed(false);
        }
    }
}

void APCMiniWindow::TestLEDs()
{
    if (app && static_cast<APCMiniGUIApp*>(app)->IsHardwareConnected()) {
        // Launch LED test in background thread to keep GUI responsive
        thread_id test_thread = spawn_thread(TestLEDsThreadEntry, "led_test_thread",
                                            B_NORMAL_PRIORITY, this);
        if (test_thread >= 0) {
            resume_thread(test_thread);
        }
    } else {
        ShowErrorMessage("Device not connected. Connect device first to test LEDs.");
    }
}

int32 APCMiniWindow::TestLEDsThreadEntry(void* data)
{
    APCMiniWindow* window = static_cast<APCMiniWindow*>(data);
    window->TestLEDsThread();
    return 0;
}

void APCMiniWindow::TestLEDsThread()
{
    // Cycle through colors on all pads (now in background thread)
    APCMiniMK2RGB test_colors[] = {
        {127, 0, 0},    // Red
        {0, 127, 0},    // Green
        {0, 0, 127},    // Blue
        {127, 127, 0},  // Yellow
        {127, 0, 127},  // Magenta
        {0, 127, 127},  // Cyan
        {127, 127, 127}, // White
        {0, 0, 0}       // Off
    };

    for (int color = 0; color < 8; color++) {
        for (int pad = 0; pad < APC_MINI_PAD_COUNT; pad++) {
            SetPadColor(pad, test_colors[color]);
        }
        snooze(200000); // 200ms delay - now safe in background thread
    }

    // Test buttons
    for (int i = 0; i < 8; i++) {
        SetTrackButtonLED(i, true);
        SetSceneButtonLED(i, true);
        snooze(100000); // 100ms - now safe in background thread
        SetTrackButtonLED(i, false);
        SetSceneButtonLED(i, false);
    }
}

void APCMiniWindow::ShowAbout()
{
    BString about_text;
    about_text << "APC Mini MK2 Controller GUI\n\n";
    about_text << "A faithful recreation of the Akai APC Mini MK2\n";
    about_text << "hardware controller for Haiku OS.\n\n";
    about_text << "Features:\n";
    about_text << "• 8x8 RGB pad matrix\n";
    about_text << "• 9 faders (8 track + 1 master)\n";
    about_text << "• Track and Scene buttons with LED feedback\n";
    about_text << "• USB Raw MIDI communication\n";
    about_text << "• Bidirectional hardware synchronization\n\n";
    about_text << "Built with the Haiku Be API";

    BAlert* alert = new BAlert("About", about_text.String(), "OK",
                               nullptr, nullptr, B_WIDTH_AS_USUAL, B_INFO_ALERT);
    alert->Go();
}