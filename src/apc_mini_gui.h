#ifndef APC_MINI_GUI_H
#define APC_MINI_GUI_H

#include <Application.h>
#include <Window.h>
#include <View.h>
#include <Button.h>
#include <Slider.h>
#include <Box.h>
#include <StringView.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Alert.h>
#include <Screen.h>
#include <Bitmap.h>
#include <Region.h>
#include <OS.h>
#include <LayoutBuilder.h>
#include <GroupLayout.h>
#include <GridLayout.h>
#include <SplitView.h>
#include <ScrollView.h>
#include <TextView.h>
#include <MidiRoster.h>
#include <MidiConsumer.h>
#include <MidiProducer.h>

#include "apc_mini_defs.h"
#include "usb_raw_midi.h"

// Forward declarations for new MIDI system
class MIDIMessageQueue;
class MIDIEventHandler;
class MIDIEventLooper;

// GUI Constants - Hardware Accurate Dimensions
#define APC_GUI_PAD_SIZE          35    // Size of each pad in pixels
#define APC_GUI_PAD_SPACING       3     // Spacing between pads
#define APC_GUI_FADER_WIDTH       35    // Width of faders (matches pad width for alignment)
#define APC_GUI_FADER_HEIGHT      200   // Height of faders
#define APC_GUI_FADER_TRACK_WIDTH 10    // Width of fader track
#define APC_GUI_FADER_KNOB_WIDTH  28    // Width of fader knob
#define APC_GUI_FADER_KNOB_HEIGHT 15    // Height of fader knob
#define APC_GUI_BUTTON_WIDTH      32    // Width of side buttons
#define APC_GUI_BUTTON_HEIGHT     24    // Height of side buttons (increased for better usability)
#define APC_GUI_SHIFT_BUTTON_SIZE 25    // Size of square shift button
#define APC_GUI_MARGIN            20    // Margin around the entire control (increased for better visual breathing room)
#define APC_GUI_CORNER_RADIUS     3     // Radius for rounded corners

// Color Constants (matching real APC Mini MK2)
#define APC_GUI_BACKGROUND_COLOR  rgb_color{18, 17, 16, 255}    // Very dark background with warm tone
#define APC_GUI_DEVICE_BODY_COLOR rgb_color{30, 29, 28, 255}    // Matte black device body with warm tone
#define APC_GUI_DEVICE_BODY_HIGHLIGHT rgb_color{42, 41, 40, 255} // Body highlight for 3D effect with warm tone
#define APC_GUI_DEVICE_BODY_SHADOW rgb_color{20, 19, 18, 255}   // Body shadow for depth with warm tone

// Pad colors - hardware accurate with proper contrast
#define APC_GUI_PAD_OFF_COLOR     rgb_color{38, 38, 38, 255}    // Dark grey when off
#define APC_GUI_PAD_BORDER_COLOR  rgb_color{220, 220, 220, 255} // White border like real hardware
#define APC_GUI_PAD_BORDER_SHADOW rgb_color{15, 15, 15, 255}    // Dark border shadow
#define APC_GUI_PAD_INNER_SHADOW  rgb_color{25, 25, 25, 255}    // Inner pad shadow
#define APC_GUI_PAD_HIGHLIGHT     rgb_color{55, 55, 55, 255}    // Pad highlight

// Fader colors - realistic silver/metal appearance
#define APC_GUI_FADER_TRACK_COLOR rgb_color{22, 22, 22, 255}    // Very dark track
#define APC_GUI_FADER_TRACK_BORDER rgb_color{45, 45, 45, 255}   // Track border
#define APC_GUI_FADER_KNOB_COLOR  rgb_color{200, 200, 200, 255} // Bright silver knob
#define APC_GUI_FADER_KNOB_HIGHLIGHT rgb_color{230, 230, 230, 255} // Knob highlight
#define APC_GUI_FADER_KNOB_SHADOW rgb_color{120, 120, 120, 255} // Knob shadow
#define APC_GUI_FADER_SCALE_COLOR rgb_color{140, 140, 140, 255} // Scale marks
#define APC_GUI_FADER_SCALE_MINOR rgb_color{100, 100, 100, 255} // Minor scale marks

// Button colors - hardware accurate
#define APC_GUI_BUTTON_OFF_COLOR  rgb_color{45, 45, 45, 255}    // Button off color
#define APC_GUI_BUTTON_BORDER     rgb_color{60, 60, 60, 255}    // Button border
#define APC_GUI_BUTTON_SHADOW     rgb_color{20, 20, 20, 255}    // Button shadow
#define APC_GUI_BUTTON_HIGHLIGHT  rgb_color{70, 70, 70, 255}    // Button highlight
#define APC_GUI_TRACK_BUTTON_ON   rgb_color{255, 50, 50, 255}   // Bright red for track buttons
#define APC_GUI_SCENE_BUTTON_ON   rgb_color{50, 255, 50, 255}   // Bright green for scene buttons
#define APC_GUI_SHIFT_BUTTON_ON   rgb_color{255, 220, 30, 255}  // Bright yellow for shift button

// Text and branding colors
#define APC_GUI_TEXT_COLOR        rgb_color{240, 240, 240, 255} // Bright white text
#define APC_GUI_LABEL_COLOR       rgb_color{180, 180, 180, 255} // Light grey labels
#define APC_GUI_BRAND_COLOR       rgb_color{255, 255, 255, 255} // White branding
#define APC_GUI_STATUS_COLOR      rgb_color{255, 255, 255, 255} // White status text

// 3D effect colors for realistic hardware appearance
#define APC_GUI_BEVEL_LIGHT       rgb_color{80, 80, 80, 255}    // Light bevel
#define APC_GUI_BEVEL_DARK        rgb_color{10, 10, 10, 255}    // Dark bevel
#define APC_GUI_SURFACE_SHINE     rgb_color{60, 60, 60, 255}    // Surface shine effect

// Message constants
enum {
    MSG_PAD_PRESSED = 'pad_',
    MSG_TRACK_BUTTON = 'trbr',
    MSG_SCENE_BUTTON = 'scbr',
    MSG_SHIFT_BUTTON = 'shbt',
    MSG_FADER_CHANGED = 'fadr',
    MSG_MENU_TOGGLE_USB = 'tusb',
    MSG_MENU_RESET_DEVICE = 'rstd',
    MSG_MENU_TEST_LEDS = 'tstl',
    MSG_MENU_HAIKU_LOGO = 'hiku',
    MSG_MENU_ABOUT = 'abut',
    MSG_MENU_DEBUG_LOG = 'dlog',
    MSG_HARDWARE_FADER_CHANGE = 'hfdr',
    MSG_HARDWARE_MIDI_EVENT = 'hmdi'
};

// Forward declarations
class APCMiniGUIApp;
class APCMiniWindow;
class PadMatrixView;
class FaderView;
class ControlButtonView;
class DebugLogWindow;
class BrandedBackgroundView;

// RGB Pad class
class RGBPad : public BView {
public:
    RGBPad(BRect frame, uint8_t pad_index);
    virtual ~RGBPad();

    virtual void Draw(BRect updateRect) override;
    virtual void MouseDown(BPoint where) override;
    virtual void MouseUp(BPoint where) override;

    void SetColor(const APCMiniMK2RGB& color);
    void SetPressed(bool pressed);
    void SetVelocity(uint8_t velocity);

    uint8_t GetPadIndex() const { return pad_index; }
    bool IsPressed() const { return is_pressed; }

private:
    uint8_t pad_index;
    APCMiniMK2RGB current_color;
    bool is_pressed;
    uint8_t velocity;
    bool mouse_down;

    rgb_color RGBToColor(const APCMiniMK2RGB& rgb);
    void SendPadMessage();
};

// Pad Matrix View (8x8 grid)
class PadMatrixView : public BView {
public:
    PadMatrixView(BRect frame);
    virtual ~PadMatrixView();

    virtual void Draw(BRect updateRect) override;
    virtual void MessageReceived(BMessage* message) override;

    void SetPadColor(uint8_t pad_index, const APCMiniMK2RGB& color);
    void SetPadPressed(uint8_t pad_index, bool pressed, uint8_t velocity = 127);
    void ResetAllPads();

    RGBPad* GetPad(uint8_t pad_index);

private:
    RGBPad* pads[APC_MINI_PAD_COUNT];

    void InitializePads();
    BRect CalculatePadFrame(uint8_t row, uint8_t col);
};

// Custom Fader Control
class FaderControl : public BView {
public:
    FaderControl(BRect frame, uint8_t fader_index, const char* label = nullptr);
    virtual ~FaderControl();

    virtual void Draw(BRect updateRect) override;
    virtual void MouseDown(BPoint where) override;
    virtual void MouseMoved(BPoint where, uint32 code, const BMessage* message) override;
    virtual void MouseUp(BPoint where) override;

    void SetValue(uint8_t value);
    uint8_t GetValue() const { return current_value; }
    uint8_t GetFaderIndex() const { return fader_index; }

private:
    uint8_t fader_index;
    uint8_t current_value;
    bool is_dragging;
    BString label;

    BRect GetSliderRect();
    BRect GetKnobRect();
    uint8_t PointToValue(BPoint point);
    BPoint ValueToPoint(uint8_t value);
    void SendFaderMessage();

    // Enhanced drawing methods
    void DrawFaderScale(BRect track_rect);
    void DrawFaderKnob(BRect knob_rect);
    void DrawFaderLabel(BRect bounds);
    void DrawValueIndicator(BRect track_rect);
};

// Fader Panel View
class FaderView : public BView {
public:
    FaderView(BRect frame);
    virtual ~FaderView();

    virtual void Draw(BRect updateRect) override;
    virtual void MessageReceived(BMessage* message) override;

    void SetFaderValue(uint8_t fader_index, uint8_t value);
    uint8_t GetFaderValue(uint8_t fader_index);
    void VerifyFaderPositions(); // Debug method to verify fader state persistence

private:
    FaderControl* track_faders[APC_MINI_TRACK_FADER_COUNT];
    FaderControl* master_fader;

    void InitializeFaders();
    BRect CalculateFaderFrame(uint8_t fader_index, bool is_master = false);
};

// Control Button (Track/Scene buttons)
class ControlButton : public BView {
public:
    enum ButtonType {
        BUTTON_TRACK,
        BUTTON_SCENE,
        BUTTON_SHIFT
    };

    ControlButton(BRect frame, uint8_t button_index, ButtonType type);
    virtual ~ControlButton();

    virtual void Draw(BRect updateRect) override;
    virtual void MouseDown(BPoint where) override;
    virtual void MouseUp(BPoint where) override;

    void SetPressed(bool pressed);
    void SetLEDOn(bool on);

    uint8_t GetButtonIndex() const { return button_index; }
    ButtonType GetButtonType() const { return button_type; }
    bool IsPressed() const { return is_pressed; }

private:
    uint8_t button_index;
    ButtonType button_type;
    bool is_pressed;
    bool led_on;
    bool mouse_down;

    rgb_color GetButtonColor();
    void SendButtonMessage();

    // Enhanced drawing methods
    void DrawButtonGradient(BRect rect, rgb_color base_color);
    void DrawButtonLabel(BRect rect);
    void DrawLEDIndicator(BRect rect);
};

// Control Buttons Panel
class ControlButtonView : public BView {
public:
    ControlButtonView(BRect frame);
    virtual ~ControlButtonView();

    virtual void Draw(BRect updateRect) override;
    virtual void MessageReceived(BMessage* message) override;

    void SetTrackButtonLED(uint8_t button_index, bool on);
    void SetSceneButtonLED(uint8_t button_index, bool on);
    void SetShiftButtonPressed(bool pressed);

private:
    ControlButton* scene_buttons[8];
    ControlButton* shift_button;

    void InitializeButtons();
    BRect CalculateButtonFrame(uint8_t index, ControlButton::ButtonType type);
};

// Main APC Mini Window
class APCMiniWindow : public BWindow {
public:
    APCMiniWindow();
    virtual ~APCMiniWindow();

    virtual void MessageReceived(BMessage* message) override;
    virtual bool QuitRequested() override;

    // Hardware interface
    void UpdateFromDevice(const APCMiniState& state);
    void HandlePadPress(uint8_t pad_index, uint8_t velocity);
    void HandlePadRelease(uint8_t pad_index);
    void UpdatePadPressDirectly(uint8_t pad_index, uint8_t velocity); // Ultra-low latency direct update
    void UpdatePadReleaseDirectly(uint8_t pad_index); // Ultra-low latency direct update
    void HandleFaderChange(uint8_t fader_index, uint8_t value);
    void UpdateFaderDirectly(uint8_t fader_index, uint8_t value); // Ultra-low latency direct update
    void UpdateTrackButtonDirectly(uint8_t button_index, bool pressed); // Ultra-low latency direct update
    void UpdateSceneButtonDirectly(uint8_t button_index, bool pressed); // Ultra-low latency direct update
    void UpdateShiftButtonDirectly(bool pressed); // Ultra-low latency direct update
    void HandleTrackButton(uint8_t button_index, bool pressed);
    void HandleSceneButton(uint8_t button_index, bool pressed);
    void HandleShiftButton(bool pressed);

    // LED/Display updates
    void SetPadColor(uint8_t pad_index, const APCMiniMK2RGB& color);
    void SetTrackButtonLED(uint8_t button_index, bool on);
    void SetSceneButtonLED(uint8_t button_index, bool on);
    void DrawHaikuLogo();
    static int32 DrawHaikuLogoThreadEntry(void* data);
    void DrawHaikuLogoThread();

    // Status
    void SetConnectionStatus(bool connected);
    void ShowErrorMessage(const char* message);

public:
    APCMiniGUIApp* app;
    PadMatrixView* pad_matrix;
    FaderView* fader_panel;
    ControlButtonView* button_panel;
    BMenuBar* menu_bar;
    BStringView* status_view;
    DebugLogWindow* debug_window;
    BrandedBackgroundView* background_view;  // Background view with AKAI branding
    // BBox* main_container; // Removed - controls added directly to window

    // Individual track buttons (not part of button_panel)
    ControlButton* track_buttons[8];

    bool is_connected;
    // Per-fader ignore flags to prevent feedback loops without blocking other faders
    bool ignore_hardware_updates[9];  // One flag per fader (8 track + 1 master)
    bigtime_t ignore_flag_timestamp[9]; // When each ignore flag was set

    void InitializeInterface();
    void CreateMenuBar();
    void CreateComponents();
    void SetupLayout();
    BView* CreateTrackButtonsGroup();
    ControlButton* CreateTrackButton(uint8_t index);
    void CreateBrandingArea();
    BRect CalculateMainFrame();
    void HandleMenuSelection(BMessage* message);
    void ToggleUSBConnection();
    void ResetDevice();
    void TestLEDs();
    static int32 TestLEDsThreadEntry(void* data);
    void TestLEDsThread();
    void ShowAbout();
};

// MIDI Endpoint Classes for Patchbay Integration
class APCMiniMIDIConsumer : public BMidiLocalConsumer {
public:
    APCMiniMIDIConsumer(class APCMiniGUIApp* app);
    virtual void NoteOn(uchar channel, uchar note, uchar velocity, bigtime_t time) override;
    virtual void NoteOff(uchar channel, uchar note, uchar velocity, bigtime_t time) override;
    virtual void ControlChange(uchar channel, uchar controller, uchar value, bigtime_t time) override;
private:
    class APCMiniGUIApp* gui_app;
};

class APCMiniMIDIProducer : public BMidiLocalProducer {
public:
    APCMiniMIDIProducer();
};

// Main Application Class
class APCMiniGUIApp : public BApplication {
    friend class APCMiniMIDIConsumer; // Allow MIDI consumer to access private methods
public:
    APCMiniGUIApp();
    virtual ~APCMiniGUIApp();

    virtual void ReadyToRun() override;
    virtual bool QuitRequested() override;
    virtual void MessageReceived(BMessage* message) override;

    // Hardware interface
    bool InitializeHardware();
    void ShutdownHardware();
    bool IsHardwareConnected() const;

    // MIDI communication
    void SendNoteOn(uint8_t note, uint8_t velocity);
    void SendNoteOff(uint8_t note);
    void SendControlChange(uint8_t controller, uint8_t value);
    void SendPadRGB(uint8_t pad_index, const APCMiniMK2RGB& color);
    void SetTrackButtonLED(uint8_t button_index, bool on);
    void SetSceneButtonLED(uint8_t button_index, bool on);

    // Device state
    const APCMiniState& GetDeviceState() const { return device_state; }
    void ResetDeviceState();

    APCMiniWindow* GetMainWindow() { return main_window; }

    // New MIDI system integration
    MIDIMessageQueue* GetMIDIQueue() { return midi_queue; }
    MIDIEventHandler* GetMIDIHandler() { return midi_handler; }

    // MIDI handling (public for thread-safe message passing)
    void HandleMIDIMessage(uint8_t status, uint8_t data1, uint8_t data2);

private:
    APCMiniWindow* main_window;
    USBRawMIDI* usb_midi;
    APCMiniState device_state;
    thread_id sync_thread;
    volatile bool should_stop;
    bool use_hardware;

    // New MIDI system components
    MIDIMessageQueue* midi_queue;
    MIDIEventHandler* midi_handler;
    MIDIEventLooper* midi_looper;

    // Patchbay integration
    APCMiniMIDIConsumer* midi_consumer;
    APCMiniMIDIProducer* midi_producer;

    // MIDI handling (private methods)
    void HandleNoteOn(uint8_t note, uint8_t velocity);
    void HandleNoteOff(uint8_t note, uint8_t velocity);
    void HandleControlChange(uint8_t controller, uint8_t value);

    // New MIDI system integration
    void RegisterMIDICallbacks();

    // Background synchronization
    static int32 SyncThreadEntry(void* data);
    void SyncThreadLoop();

    // Utilities
    void UpdateGUIFromState();
    void InitializeDeviceState();
    void QueryFaderPositions(); // Interrogate hardware for current fader positions
    uint8_t ScanSingleFader(uint8_t cc_number); // Scan individual fader position
    rgb_color MIDIVelocityToRGB(uint8_t velocity);
    APCMiniMK2RGB VelocityToMK2RGB(uint8_t velocity);
};

// Debug Log Window for raw MIDI messages
class DebugLogWindow : public BWindow {
public:
    DebugLogWindow();
    virtual ~DebugLogWindow();

    virtual void MessageReceived(BMessage* message) override;
    virtual bool QuitRequested() override;

    // Log MIDI messages
    void LogMIDIMessage(const char* direction, uint8_t status, uint8_t data1, uint8_t data2);
    void LogRawData(const char* direction, const uint8_t* data, size_t length);
    void LogStatusMessage(const char* message);
    void ClearLog();

private:
    BScrollView* scroll_view;
    BTextView* log_text;
    BButton* clear_button;
    BStringView* status_label;

    void InitializeInterface();
    void AppendLogLine(const char* line);

    static const int MAX_LOG_LINES = 1000;
    int current_lines;
};

// Branded Background View with AKAI branding like real hardware
class BrandedBackgroundView : public BView {
public:
    BrandedBackgroundView(BRect frame);
    virtual ~BrandedBackgroundView();

    virtual void Draw(BRect updateRect) override;

private:
    void DrawDeviceBody(BRect bounds);
    void DrawAKAIBranding(BRect bounds);
    void DrawModelLabels(BRect bounds);
    void DrawTexturedSurface(BRect bounds);
    void DrawRealisticShadows(BRect bounds);
};

#endif // APC_MINI_GUI_H