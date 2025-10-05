#ifndef APC_MINI_DEFS_H
#define APC_MINI_DEFS_H

#include <stdint.h>

// USB Vendor/Product IDs
#define APC_MINI_VENDOR_ID      0x09E8  // Akai Professional M.I. Corp.
#define APC_MINI_PRODUCT_ID     0x0028  // APC Mini (original)
#define APC_MINI_MK2_PRODUCT_ID 0x004F  // APC Mini MK2 (verified from Windows)

// MIDI Channel (0-based)
#define APC_MINI_MIDI_CHANNEL 0

// Pad Matrix Configuration (8x8 = 64 pads)
#define APC_MINI_PAD_ROWS 8
#define APC_MINI_PAD_COLS 8
#define APC_MINI_PAD_COUNT (APC_MINI_PAD_ROWS * APC_MINI_PAD_COLS)

// MIDI Note Numbers
#define APC_MINI_PAD_NOTE_START     0x00  // 0
#define APC_MINI_PAD_NOTE_END       0x3F  // 63
#define APC_MINI_TRACK_NOTE_START   0x64  // 100
#define APC_MINI_TRACK_NOTE_END     0x6B  // 107
#define APC_MINI_SCENE_NOTE_START   0x70  // 112
#define APC_MINI_SCENE_NOTE_END     0x77  // 119
#define APC_MINI_SHIFT_NOTE         0x7A  // 122

// Control Change Numbers for Faders
// APC Mini Physical Layout: [F1] [F2] [F3] [F4] [F5] [F6] [F7] [F8] [MASTER]
//                    CC:     48   49   50   51   52   53   54   55     56
#define APC_MINI_FADER_CC_START     0x30  // 48 (Track Fader 1)
#define APC_MINI_FADER_CC_END       0x37  // 55 (Track Fader 8)
#define APC_MINI_MASTER_CC          0x38  // 56 (Master Fader)
#define APC_MINI_TRACK_FADER_COUNT  8     // Track faders only (1-8)
#define APC_MINI_TOTAL_FADER_COUNT  9     // Track faders + Master

// LED Colors - Original APC Mini
enum APCMiniLEDColor {
    APC_LED_OFF         = 0x00,
    APC_LED_GREEN       = 0x01,
    APC_LED_GREEN_BLINK = 0x02,
    APC_LED_RED         = 0x03,
    APC_LED_RED_BLINK   = 0x04,
    APC_LED_YELLOW      = 0x05,
    APC_LED_YELLOW_BLINK= 0x06
};

// LED Colors - APC Mini MK2 RGB Support
enum APCMiniMK2LEDMode {
    APC_MK2_LED_MODE_LEGACY = 0,  // Use original 7-color mode
    APC_MK2_LED_MODE_RGB = 1      // Use full RGB mode via SysEx
};

// MK2 Operating Modes
enum APCMiniMK2Mode {
    APC_MK2_MODE_SESSION = 0,      // Classic session mode (like original)
    APC_MK2_MODE_NOTE = 1,          // Note mode - pads play chromatic notes
    APC_MK2_MODE_DRUM = 2           // Drum mode - pads play drum sounds
};

// NOTE: Navigation arrows removed - not present in official MK2 protocol

// MK2 SysEx Header for RGB LED Control (Official Protocol)
#define APC_MK2_SYSEX_HEADER    0xF0, 0x47, 0x7F, 0x4F  // Manufacturer ID + Product ID (0x4F)
#define APC_MK2_SYSEX_RGB_CMD   0x24  // RGB LED Color Lighting command
#define APC_MK2_SYSEX_INTRO_CMD 0x60  // Introduction Message command
#define APC_MK2_SYSEX_INTRO_RESP 0x61  // Introduction Response command
#define APC_MK2_SYSEX_MODE_CMD  0x62  // Mode Change command (Session/Note/Drum)
#define APC_MK2_SYSEX_END       0xF7

// MK2 Note Mode Settings
#define APC_MK2_NOTE_MODE_ROOT_NOTE     0x24  // C3 (36) as root note
#define APC_MK2_DRUM_MODE_BASE_NOTE     0x24  // C3 (36) as drum base

// MK2 Scale Types for Note Mode
enum APCMiniMK2Scale {
    APC_MK2_SCALE_CHROMATIC = 0,    // All semitones
    APC_MK2_SCALE_MAJOR = 1,        // Major scale
    APC_MK2_SCALE_MINOR = 2,        // Natural minor
    APC_MK2_SCALE_PENTATONIC = 3,   // Pentatonic scale
    APC_MK2_SCALE_BLUES = 4         // Blues scale
};

// Drum Kit Layout for Drum Mode (GM Standard)
enum APCMiniMK2DrumKit {
    APC_MK2_DRUM_KICK = 36,         // C2 - Kick Drum
    APC_MK2_DRUM_SNARE = 38,        // D2 - Snare
    APC_MK2_DRUM_HIHAT_CLOSED = 42, // F#2 - Closed Hi-Hat
    APC_MK2_DRUM_HIHAT_OPEN = 46,   // A#2 - Open Hi-Hat
    APC_MK2_DRUM_CRASH = 49,        // C#3 - Crash Cymbal
    APC_MK2_DRUM_RIDE = 51,         // D#3 - Ride Cymbal
    APC_MK2_DRUM_TOM_HIGH = 48,     // C3 - High Tom
    APC_MK2_DRUM_TOM_MID = 45,      // A2 - Mid Tom
    APC_MK2_DRUM_TOM_LOW = 41       // F2 - Low Tom
};

// RGB Color Structure for MK2
struct APCMiniMK2RGB {
    uint8_t red;    // 0-127 (7-bit MIDI)
    uint8_t green;  // 0-127 (7-bit MIDI)
    uint8_t blue;   // 0-127 (7-bit MIDI)
};

// MK2 Preset RGB Colors (128 colors from official protocol)
// These colors correspond to velocity values 0-127 in MIDI Note-On messages
extern const APCMiniMK2RGB APC_MK2_PRESET_COLORS[128];

// USB MIDI Event Packet Structure
struct USBMIDIEventPacket {
    uint8_t header;     // Cable Number (4 bits) + Code Index Number (4 bits)
    uint8_t midi[3];    // Standard MIDI bytes: Status, Data1, Data2
} __attribute__((packed));

// MIDI Message Types
#define MIDI_NOTE_OFF           0x80
#define MIDI_NOTE_ON            0x90
#define MIDI_CONTROL_CHANGE     0xB0

// USB MIDI Code Index Numbers
#define USB_MIDI_CIN_NOTE_OFF   0x08
#define USB_MIDI_CIN_NOTE_ON    0x09
#define USB_MIDI_CIN_CC         0x0B

// Test Configuration
#define MAX_LATENCY_MS          10
#define STRESS_TEST_MESSAGES    1000
#define USB_TRANSFER_TIMEOUT_MS 100

// Pad Layout Helper Macros
#define PAD_XY_TO_NOTE(x, y)    ((y) * APC_MINI_PAD_COLS + (x))
#define PAD_NOTE_TO_X(note)     ((note) % APC_MINI_PAD_COLS)
#define PAD_NOTE_TO_Y(note)     ((note) / APC_MINI_PAD_COLS)

// Track/Scene Button Helper Macros
#define IS_PAD_NOTE(note)       ((note) >= APC_MINI_PAD_NOTE_START && (note) <= APC_MINI_PAD_NOTE_END)
#define IS_TRACK_NOTE(note)     ((note) >= APC_MINI_TRACK_NOTE_START && (note) <= APC_MINI_TRACK_NOTE_END)
#define IS_SCENE_NOTE(note)     ((note) >= APC_MINI_SCENE_NOTE_START && (note) <= APC_MINI_SCENE_NOTE_END)
#define IS_SHIFT_NOTE(note)     ((note) == APC_MINI_SHIFT_NOTE)
#define IS_TRACK_FADER_CC(cc)   ((cc) >= APC_MINI_FADER_CC_START && (cc) <= APC_MINI_FADER_CC_END)
#define IS_MASTER_FADER_CC(cc)  ((cc) == APC_MINI_MASTER_CC)
#define IS_ANY_FADER_CC(cc)     (IS_TRACK_FADER_CC(cc) || IS_MASTER_FADER_CC(cc))

// Performance Monitoring
struct APCMiniStats {
    uint32_t messages_received;
    uint32_t messages_sent;
    uint32_t pad_presses;
    uint32_t fader_moves;
    uint32_t button_presses;
    uint64_t total_latency_us;
    uint32_t max_latency_us;
    uint32_t min_latency_us;
    uint32_t error_count;
};

// Device State
struct APCMiniState {
    bool pads[APC_MINI_PAD_COUNT];
    uint8_t pad_velocities[APC_MINI_PAD_COUNT];
    uint8_t pad_colors[APC_MINI_PAD_COUNT];
    uint8_t track_fader_values[APC_MINI_TRACK_FADER_COUNT];  // Track faders 1-8
    uint8_t master_fader_value;                               // Master fader
    bool track_buttons[8];
    bool scene_buttons[8];
    bool shift_pressed;
    APCMiniStats stats;

    // MK2 specific state
    bool is_mk2_device;                        // Detected as MK2
    APCMiniMK2LEDMode led_mode;               // RGB vs Legacy LED mode
    APCMiniMK2Mode device_mode;               // Session/Note/Drum mode
    APCMiniMK2Scale current_scale;            // Current scale in Note mode
    uint8_t root_note;                        // Root note for scales
    APCMiniMK2RGB pad_rgb_colors[APC_MINI_PAD_COUNT];  // RGB colors for MK2
    uint8_t note_mode_notes[APC_MINI_PAD_COUNT];  // Note numbers in Note mode
    uint8_t drum_mode_notes[APC_MINI_PAD_COUNT];  // Drum notes in Drum mode
};

// Test Modes
enum APCMiniTestMode {
    TEST_MODE_INTERACTIVE,
    TEST_MODE_SIMULATION,
    TEST_MODE_STRESS,
    TEST_MODE_LATENCY
};

// Error Codes
enum APCMiniError {
    APC_SUCCESS = 0,
    APC_ERROR_DEVICE_NOT_FOUND,
    APC_ERROR_USB_OPEN_FAILED,
    APC_ERROR_USB_CLAIM_FAILED,
    APC_ERROR_USB_TRANSFER_FAILED,
    APC_ERROR_MIDI_INIT_FAILED,
    APC_ERROR_THREAD_CREATE_FAILED,
    APC_ERROR_INVALID_PARAMETER,
    APC_ERROR_TIMEOUT
};

#endif // APC_MINI_DEFS_H