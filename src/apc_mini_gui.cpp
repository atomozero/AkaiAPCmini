#include "apc_mini_gui.h"
#include <cstdio>
#include <cstring>
#include <cmath>

// ===============================
// RGBPad Implementation
// ===============================

RGBPad::RGBPad(BRect frame, uint8_t pad_index)
    : BView(frame, "rgb_pad", B_FOLLOW_NONE, B_WILL_DRAW | B_FRAME_EVENTS)
    , pad_index(pad_index)
    , current_color({0, 0, 0})
    , is_pressed(false)
    , velocity(0)
    , mouse_down(false)
{
    SetViewColor(APC_GUI_PAD_OFF_COLOR); // Dark gray for off pads
}

RGBPad::~RGBPad()
{
}

void RGBPad::Draw(BRect /*updateRect*/)
{
    BRect bounds = Bounds();
    rgb_color pad_color;

    if (is_pressed) {
        // Brighten the color when pressed
        pad_color = RGBToColor(current_color);
        pad_color.red = min_c(255, pad_color.red + 50);
        pad_color.green = min_c(255, pad_color.green + 50);
        pad_color.blue = min_c(255, pad_color.blue + 50);
    } else {
        pad_color = RGBToColor(current_color);
    }

    // Default to dark grey if color is black
    if (pad_color.red == 0 && pad_color.green == 0 && pad_color.blue == 0) {
        pad_color = APC_GUI_PAD_OFF_COLOR;
    }

    // Draw outer shadow for depth (like real hardware)
    SetHighColor(APC_GUI_PAD_BORDER_SHADOW);
    BRect shadow_rect = bounds;
    shadow_rect.OffsetBy(1, 1);
    FillRect(shadow_rect);

    // Draw main pad area with realistic inset effect
    BRect pad_rect = bounds;
    pad_rect.InsetBy(1, 1);

    // Create 3D inset effect - darker at top-left, lighter at bottom-right
    rgb_color top_left_color, bottom_right_color;

    if (is_pressed) {
        // When pressed, invert the 3D effect to look "pushed in"
        top_left_color = APC_GUI_PAD_HIGHLIGHT;
        bottom_right_color = APC_GUI_PAD_INNER_SHADOW;
    } else {
        // Normal raised appearance
        top_left_color = APC_GUI_PAD_INNER_SHADOW;
        bottom_right_color = APC_GUI_PAD_HIGHLIGHT;
    }

    // Draw top and left inner borders (3D effect)
    SetHighColor(top_left_color);
    StrokeLine(BPoint(pad_rect.left, pad_rect.top), BPoint(pad_rect.right-1, pad_rect.top));
    StrokeLine(BPoint(pad_rect.left, pad_rect.top), BPoint(pad_rect.left, pad_rect.bottom-1));

    // Draw bottom and right inner borders (3D effect)
    SetHighColor(bottom_right_color);
    StrokeLine(BPoint(pad_rect.left+1, pad_rect.bottom), BPoint(pad_rect.right, pad_rect.bottom));
    StrokeLine(BPoint(pad_rect.right, pad_rect.top+1), BPoint(pad_rect.right, pad_rect.bottom));

    // Fill the main pad area with gradient
    BRect fill_rect = pad_rect;
    fill_rect.InsetBy(1, 1);

    // Create subtle gradient effect for realism
    rgb_color top_color = pad_color;
    rgb_color bottom_color = pad_color;

    if (!is_pressed) {
        // Normal state: lighter at top
        top_color.red = min_c(255, pad_color.red + 15);
        top_color.green = min_c(255, pad_color.green + 15);
        top_color.blue = min_c(255, pad_color.blue + 15);

        bottom_color.red = max_c(0, pad_color.red - 15);
        bottom_color.green = max_c(0, pad_color.green - 15);
        bottom_color.blue = max_c(0, pad_color.blue - 15);
    } else {
        // Pressed state: more uniform color
        top_color = pad_color;
        bottom_color = pad_color;
    }

    // Draw gradient effect
    for (float y = fill_rect.top; y <= fill_rect.bottom; y++) {
        float ratio = (y - fill_rect.top) / fill_rect.Height();
        rgb_color line_color;
        line_color.red = (uint8_t)(top_color.red * (1.0f - ratio) + bottom_color.red * ratio);
        line_color.green = (uint8_t)(top_color.green * (1.0f - ratio) + bottom_color.green * ratio);
        line_color.blue = (uint8_t)(top_color.blue * (1.0f - ratio) + bottom_color.blue * ratio);
        line_color.alpha = 255;

        SetHighColor(line_color);
        StrokeLine(BPoint(fill_rect.left, y), BPoint(fill_rect.right, y));
    }

    // Draw prominent white border like real APC Mini MK2
    SetHighColor(APC_GUI_PAD_BORDER_COLOR);
    SetPenSize(1.0);
    StrokeRect(bounds);

    // Add subtle highlight when pressed
    if (is_pressed) {
        SetHighColor(255, 255, 255, 120);
        BRect highlight = fill_rect;
        highlight.InsetBy(1, 1);
        StrokeRect(highlight);
    }

    // Draw pad number in corner (smaller and less prominent)
    rgb_color text_color = (current_color.red == 0 && current_color.green == 0 && current_color.blue == 0) ?
                          APC_GUI_LABEL_COLOR : rgb_color{255, 255, 255, 200};
    SetHighColor(text_color);
    SetFontSize(7);
    BString pad_text;
    pad_text << pad_index;
    DrawString(pad_text.String(), BPoint(2, 10));
}

void RGBPad::MouseDown(BPoint /*where*/)
{
    mouse_down = true;
    SetPressed(true);
    SendPadMessage();
}

void RGBPad::MouseUp(BPoint /*where*/)
{
    if (mouse_down) {
        mouse_down = false;
        SetPressed(false);

        BMessage msg(MSG_PAD_PRESSED);
        msg.AddInt32("pad_index", pad_index);
        msg.AddBool("pressed", false);
        Window()->PostMessage(&msg);
    }
}

void RGBPad::SetColor(const APCMiniMK2RGB& color)
{
    current_color = color;
    Invalidate();
}

void RGBPad::SetPressed(bool pressed)
{
    if (is_pressed != pressed) {
        is_pressed = pressed;
        Invalidate();
    }
}

void RGBPad::SetVelocity(uint8_t new_velocity)
{
    velocity = new_velocity;
}

rgb_color RGBPad::RGBToColor(const APCMiniMK2RGB& rgb)
{
    // Convert 7-bit MIDI values (0-127) to 8-bit RGB (0-255)
    return rgb_color{
        static_cast<uint8_t>((rgb.red * 255) / 127),
        static_cast<uint8_t>((rgb.green * 255) / 127),
        static_cast<uint8_t>((rgb.blue * 255) / 127),
        255
    };
}

void RGBPad::SendPadMessage()
{
    BMessage msg(MSG_PAD_PRESSED);
    msg.AddInt32("pad_index", pad_index);
    msg.AddBool("pressed", is_pressed);
    msg.AddInt32("velocity", is_pressed ? 127 : 0);
    Window()->PostMessage(&msg);
}

// ===============================
// PadMatrixView Implementation
// ===============================

PadMatrixView::PadMatrixView(BRect frame)
    : BView(frame, "pad_matrix", B_FOLLOW_NONE, B_WILL_DRAW)
{
    SetViewColor(APC_GUI_BACKGROUND_COLOR);
    InitializePads();

    // Set explicit size for layout system - accurate 8x8 pad matrix dimensions
    float matrix_width = 8 * APC_GUI_PAD_SIZE + 7 * APC_GUI_PAD_SPACING;  // 8 pads + 7 spaces between
    float matrix_height = 8 * APC_GUI_PAD_SIZE + 7 * APC_GUI_PAD_SPACING; // 8 pads + 7 spaces between
    SetExplicitMinSize(BSize(matrix_width, matrix_height));
    SetExplicitMaxSize(BSize(matrix_width, matrix_height));
    SetExplicitPreferredSize(BSize(matrix_width, matrix_height));
}

PadMatrixView::~PadMatrixView()
{
    // Pads are automatically deleted as child views
}

void PadMatrixView::Draw(BRect updateRect)
{
    // Draw background
    SetHighColor(APC_GUI_BACKGROUND_COLOR);
    FillRect(updateRect);
}

void PadMatrixView::MessageReceived(BMessage* message)
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

                // Forward to parent window
                Window()->PostMessage(message);
            }
            break;
        }
        default:
            BView::MessageReceived(message);
            break;
    }
}

void PadMatrixView::SetPadColor(uint8_t pad_index, const APCMiniMK2RGB& color)
{
    if (pad_index < APC_MINI_PAD_COUNT) {
        pads[pad_index]->SetColor(color);
    }
}

void PadMatrixView::SetPadPressed(uint8_t pad_index, bool pressed, uint8_t velocity)
{
    if (pad_index < APC_MINI_PAD_COUNT) {
        pads[pad_index]->SetPressed(pressed);
        pads[pad_index]->SetVelocity(velocity);
    }
}

void PadMatrixView::ResetAllPads()
{
    APCMiniMK2RGB off_color = {0, 0, 0};
    for (int i = 0; i < APC_MINI_PAD_COUNT; i++) {
        pads[i]->SetColor(off_color);
        pads[i]->SetPressed(false);
    }
}

RGBPad* PadMatrixView::GetPad(uint8_t pad_index)
{
    if (pad_index < APC_MINI_PAD_COUNT) {
        return pads[pad_index];
    }
    return nullptr;
}

void PadMatrixView::InitializePads()
{
    for (int row = 0; row < APC_MINI_PAD_ROWS; row++) {
        for (int col = 0; col < APC_MINI_PAD_COLS; col++) {
            uint8_t pad_index = PAD_XY_TO_NOTE(col, 7 - row); // Flip Y coordinate to match hardware
            BRect pad_frame = CalculatePadFrame(row, col);

            pads[pad_index] = new RGBPad(pad_frame, pad_index);
            AddChild(pads[pad_index]);
        }
    }
}

BRect PadMatrixView::CalculatePadFrame(uint8_t row, uint8_t col)
{
    float x = col * (APC_GUI_PAD_SIZE + APC_GUI_PAD_SPACING);
    float y = row * (APC_GUI_PAD_SIZE + APC_GUI_PAD_SPACING);

    return BRect(x, y, x + APC_GUI_PAD_SIZE - 1, y + APC_GUI_PAD_SIZE - 1);
}

// ===============================
// FaderControl Implementation
// ===============================

FaderControl::FaderControl(BRect frame, uint8_t fader_index, const char* label)
    : BView(frame, "fader_control", B_FOLLOW_NONE, B_WILL_DRAW | B_FRAME_EVENTS)
    , fader_index(fader_index)
    , current_value(0)
    , is_dragging(false)
    , label(label ? label : "")
{
    SetViewColor(APC_GUI_DEVICE_BODY_COLOR);
}

FaderControl::~FaderControl()
{
}

void FaderControl::Draw(BRect /*updateRect*/)
{
    BRect bounds = Bounds();

    // Draw fader track (vertical slot) with realistic metal appearance
    BRect track_rect = GetSliderRect();

    // Draw track outer shadow for depth
    SetHighColor(APC_GUI_BEVEL_DARK);
    BRect shadow_rect = track_rect;
    shadow_rect.OffsetBy(2, 2);
    FillRect(shadow_rect);

    // Draw track with inset 3D effect (like a metal groove)
    // Dark top/left edges for inset appearance
    SetHighColor(APC_GUI_FADER_TRACK_COLOR);
    FillRect(track_rect);

    // Add realistic track beveling - darker on top/left, lighter on bottom/right
    SetHighColor(APC_GUI_BEVEL_DARK);
    StrokeLine(BPoint(track_rect.left, track_rect.top),
              BPoint(track_rect.right-1, track_rect.top));
    StrokeLine(BPoint(track_rect.left, track_rect.top),
              BPoint(track_rect.left, track_rect.bottom-1));

    SetHighColor(APC_GUI_FADER_TRACK_BORDER);
    StrokeLine(BPoint(track_rect.left+1, track_rect.bottom),
              BPoint(track_rect.right, track_rect.bottom));
    StrokeLine(BPoint(track_rect.right, track_rect.top+1),
              BPoint(track_rect.right, track_rect.bottom));

    // Draw main track border
    SetHighColor(APC_GUI_FADER_TRACK_BORDER);
    StrokeRect(track_rect);

    // Draw realistic scale marks on the side
    DrawFaderScale(track_rect);

    // Draw fader knob with enhanced 3D metallic effect
    BRect knob_rect = GetKnobRect();
    DrawFaderKnob(knob_rect);

    // Draw fader number/label
    DrawFaderLabel(bounds);

    // Draw value indicator (LED-style bar)
    DrawValueIndicator(track_rect);
}

void FaderControl::DrawFaderScale(BRect track_rect)
{
    // Draw major scale marks every 25% (like real hardware)
    SetHighColor(APC_GUI_FADER_SCALE_COLOR);
    for (int i = 0; i <= 4; i++) {
        float y = track_rect.bottom - (i / 4.0f) * track_rect.Height();
        BPoint start(track_rect.right + 2, y);
        BPoint end(track_rect.right + 8, y);  // Longer major marks
        StrokeLine(start, end);

        // Draw scale value labels
        if (i == 0 || i == 4) {
            SetHighColor(APC_GUI_LABEL_COLOR);
            SetFontSize(6);
            BString value_text;
            value_text << (i * 25);
            DrawString(value_text.String(), BPoint(track_rect.right + 10, y + 2));
        }
    }

    // Draw minor scale marks every 12.5% (between major marks)
    SetHighColor(APC_GUI_FADER_SCALE_MINOR);
    for (int i = 0; i < 8; i++) {
        if (i % 2 == 0) continue; // Skip major mark positions
        float y = track_rect.bottom - (i / 8.0f) * track_rect.Height();
        BPoint start(track_rect.right + 2, y);
        BPoint end(track_rect.right + 5, y);  // Shorter minor marks
        StrokeLine(start, end);
    }

    // Add "dB" label at top for professional look
    SetHighColor(APC_GUI_LABEL_COLOR);
    SetFontSize(5);
    DrawString("dB", BPoint(track_rect.right + 10, track_rect.top + 8));
}

void FaderControl::DrawFaderKnob(BRect knob_rect)
{
    // Draw knob outer shadow for depth
    BRect shadow_rect = knob_rect;
    shadow_rect.OffsetBy(3, 3);
    SetHighColor(APC_GUI_FADER_KNOB_SHADOW);
    FillRect(shadow_rect);

    // Draw main knob with realistic metallic gradient
    rgb_color knob_top = APC_GUI_FADER_KNOB_HIGHLIGHT;
    rgb_color knob_middle = APC_GUI_FADER_KNOB_COLOR;
    rgb_color knob_bottom = APC_GUI_FADER_KNOB_SHADOW;

    // Create realistic metal gradient (bright at top, darker in middle, dark at bottom)
    for (float y = knob_rect.top; y <= knob_rect.bottom; y++) {
        float ratio = (y - knob_rect.top) / knob_rect.Height();
        rgb_color line_color;

        if (ratio < 0.3f) {
            // Top portion - bright highlight
            float top_ratio = ratio / 0.3f;
            line_color.red = (uint8_t)(knob_top.red * (1.0f - top_ratio) + knob_middle.red * top_ratio);
            line_color.green = (uint8_t)(knob_top.green * (1.0f - top_ratio) + knob_middle.green * top_ratio);
            line_color.blue = (uint8_t)(knob_top.blue * (1.0f - top_ratio) + knob_middle.blue * top_ratio);
        } else {
            // Bottom portion - darker gradient
            float bottom_ratio = (ratio - 0.3f) / 0.7f;
            line_color.red = (uint8_t)(knob_middle.red * (1.0f - bottom_ratio) + knob_bottom.red * bottom_ratio);
            line_color.green = (uint8_t)(knob_middle.green * (1.0f - bottom_ratio) + knob_bottom.green * bottom_ratio);
            line_color.blue = (uint8_t)(knob_middle.blue * (1.0f - bottom_ratio) + knob_bottom.blue * bottom_ratio);
        }
        line_color.alpha = 255;

        SetHighColor(line_color);
        StrokeLine(BPoint(knob_rect.left, y), BPoint(knob_rect.right, y));
    }

    // Draw 3D beveled edges for realistic hardware appearance
    // Light edges on top/left (raised effect)
    SetHighColor(APC_GUI_BEVEL_LIGHT);
    StrokeLine(BPoint(knob_rect.left, knob_rect.top),
              BPoint(knob_rect.right-1, knob_rect.top));
    StrokeLine(BPoint(knob_rect.left, knob_rect.top),
              BPoint(knob_rect.left, knob_rect.bottom-1));

    // Dark edges on bottom/right
    SetHighColor(APC_GUI_BEVEL_DARK);
    StrokeLine(BPoint(knob_rect.left+1, knob_rect.bottom),
              BPoint(knob_rect.right, knob_rect.bottom));
    StrokeLine(BPoint(knob_rect.right, knob_rect.top+1),
              BPoint(knob_rect.right, knob_rect.bottom));

    // Draw main knob border
    SetHighColor(APC_GUI_FADER_KNOB_SHADOW);
    StrokeRect(knob_rect);

    // Draw realistic grip lines on knob (like real hardware)
    SetHighColor(APC_GUI_BEVEL_DARK);
    float center_y = knob_rect.top + knob_rect.Height() / 2;

    // Multiple grip lines for realism
    for (int i = -2; i <= 2; i++) {
        if (i == 0) continue; // Skip center line for better appearance
        BPoint start(knob_rect.left + 2, center_y + i * 1.5f);
        BPoint end(knob_rect.right - 2, center_y + i * 1.5f);
        StrokeLine(start, end);
    }

    // Add subtle highlight shine on top of knob
    SetHighColor(255, 255, 255, 100);
    BRect shine = knob_rect;
    shine.InsetBy(1, 1);
    shine.bottom = shine.top + 2;
    FillRect(shine);
}

void FaderControl::DrawFaderLabel(BRect bounds)
{
    // Draw fader number at bottom
    SetHighColor(APC_GUI_TEXT_COLOR);

    // Use bold font for better readability
    BFont font;
    GetFont(&font);
    font.SetFace(B_BOLD_FACE);
    SetFont(&font);
    SetFontSize(12);  // Increased from 10 for better visibility

    BString fader_number;
    if (fader_index < APC_MINI_TRACK_FADER_COUNT) {
        fader_number << (fader_index + 1);
    } else {
        fader_number = "M";  // Master fader
    }

    float string_width = StringWidth(fader_number.String());
    BPoint label_point(bounds.Width() / 2 - string_width / 2, bounds.bottom - 5);
    DrawString(fader_number.String(), label_point);
}

void FaderControl::DrawValueIndicator(BRect track_rect)
{
    // Draw LED-style value indicator
    float fill_height = (current_value / 127.0f) * track_rect.Height();
    if (fill_height > 2) {
        BRect fill_rect = track_rect;
        fill_rect.InsetBy(1, 1);
        fill_rect.top = fill_rect.bottom - fill_height;

        // Color based on value (green low, yellow mid, red high)
        rgb_color led_color;
        if (current_value < 42) {
            led_color = rgb_color{0, 200, 0, 255};     // Green
        } else if (current_value < 85) {
            led_color = rgb_color{200, 200, 0, 255};   // Yellow
        } else {
            led_color = rgb_color{200, 0, 0, 255};     // Red
        }

        SetHighColor(led_color);
        FillRect(fill_rect);
    }
}

void FaderControl::MouseDown(BPoint where)
{
    is_dragging = true;
    SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);

    // Set value based on click position
    uint8_t new_value = PointToValue(where);
    if (new_value != current_value) {
        SetValue(new_value);
        SendFaderMessage();
    }
}

void FaderControl::MouseMoved(BPoint where, uint32 /*code*/, const BMessage* /*message*/)
{
    if (is_dragging) {
        uint8_t new_value = PointToValue(where);
        if (new_value != current_value) {
            SetValue(new_value);
            SendFaderMessage();
        }
    }
}

void FaderControl::MouseUp(BPoint /*where*/)
{
    is_dragging = false;
}

void FaderControl::SetValue(uint8_t value)
{
    if (current_value != value) {
        current_value = value;
        Invalidate();
    }
}

BRect FaderControl::GetSliderRect()
{
    BRect bounds = Bounds();
    float track_width = APC_GUI_FADER_TRACK_WIDTH;
    float center_x = bounds.Width() / 2;

    return BRect(center_x - track_width / 2, 15,
                 center_x + track_width / 2, bounds.bottom - 50);
}

BRect FaderControl::GetKnobRect()
{
    BRect track_rect = GetSliderRect();
    float knob_height = APC_GUI_FADER_KNOB_HEIGHT;
    float knob_width = APC_GUI_FADER_KNOB_WIDTH;

    float knob_y = track_rect.bottom - (current_value / 127.0f) * track_rect.Height() - knob_height / 2;
    float center_x = track_rect.left + track_rect.Width() / 2;

    return BRect(center_x - knob_width / 2, knob_y,
                 center_x + knob_width / 2, knob_y + knob_height);
}

uint8_t FaderControl::PointToValue(BPoint point)
{
    BRect track_rect = GetSliderRect();

    if (point.y <= track_rect.top) return 127;
    if (point.y >= track_rect.bottom) return 0;

    float ratio = (track_rect.bottom - point.y) / track_rect.Height();
    return static_cast<uint8_t>(ratio * 127);
}

BPoint FaderControl::ValueToPoint(uint8_t value)
{
    BRect track_rect = GetSliderRect();
    float y = track_rect.bottom - (value / 127.0f) * track_rect.Height();
    return BPoint(track_rect.left + track_rect.Width() / 2, y);
}

void FaderControl::SendFaderMessage()
{
    BMessage msg(MSG_FADER_CHANGED);
    msg.AddInt32("fader_index", fader_index);
    msg.AddInt32("value", current_value);
    Window()->PostMessage(&msg);
}

// ===============================
// FaderView Implementation
// ===============================

FaderView::FaderView(BRect frame)
    : BView(frame, "fader_view", B_FOLLOW_NONE, B_WILL_DRAW)
    , master_fader(nullptr)
{
    SetViewColor(APC_GUI_BACKGROUND_COLOR);
    InitializeFaders();

    // Set explicit size for layout system - ensure faders span full width properly
    float fader_spacing = 5;
    float fader_panel_width = 8 * (APC_GUI_FADER_WIDTH + fader_spacing) + APC_GUI_FADER_WIDTH + 20; // 8 track + master + extra spacing
    float fader_panel_height = APC_GUI_FADER_HEIGHT + 40; // Fader height + labels + padding
    SetExplicitMinSize(BSize(fader_panel_width, fader_panel_height));
    SetExplicitMaxSize(BSize(fader_panel_width, fader_panel_height));
    SetExplicitPreferredSize(BSize(fader_panel_width, fader_panel_height));
}

FaderView::~FaderView()
{
    // Faders are automatically deleted as child views
}

void FaderView::Draw(BRect updateRect)
{
    // Draw background
    SetHighColor(APC_GUI_BACKGROUND_COLOR);
    FillRect(updateRect);

    // Note: Fader labels are now drawn by individual FaderControl objects
    // to avoid duplicate numbering. Each FaderControl draws its own label
    // in FaderControl::DrawFaderLabel()
}

void FaderView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_FADER_CHANGED:
        {
            int32 fader_index;
            int32 value;

            if (message->FindInt32("fader_index", &fader_index) == B_OK &&
                message->FindInt32("value", &value) == B_OK) {

                // Forward to parent window
                Window()->PostMessage(message);
            }
            break;
        }
        default:
            BView::MessageReceived(message);
            break;
    }
}

void FaderView::SetFaderValue(uint8_t fader_index, uint8_t value)
{
    if (fader_index < APC_MINI_TRACK_FADER_COUNT) {
        track_faders[fader_index]->SetValue(value);
    } else if (fader_index == APC_MINI_TRACK_FADER_COUNT) {
        // Master fader
        master_fader->SetValue(value);
    }
}

uint8_t FaderView::GetFaderValue(uint8_t fader_index)
{
    if (fader_index < APC_MINI_TRACK_FADER_COUNT) {
        return track_faders[fader_index]->GetValue();
    } else if (fader_index == APC_MINI_TRACK_FADER_COUNT) {
        return master_fader->GetValue();
    }
    return 0;
}

void FaderView::VerifyFaderPositions()
{
    // Verification now runs silently to avoid console spam
}

void FaderView::InitializeFaders()
{
    // Track faders
    for (int i = 0; i < APC_MINI_TRACK_FADER_COUNT; i++) {
        BRect fader_frame = CalculateFaderFrame(i);
        BString fader_name;
        fader_name << "Track " << (i + 1);

        track_faders[i] = new FaderControl(fader_frame, i, fader_name.String());
        AddChild(track_faders[i]);
    }

    // Master fader
    BRect master_frame = CalculateFaderFrame(0, true);
    master_fader = new FaderControl(master_frame, APC_MINI_TRACK_FADER_COUNT, "Master");
    AddChild(master_fader);
}

BRect FaderView::CalculateFaderFrame(uint8_t fader_index, bool is_master)
{
    float x, y, width, height;

    width = APC_GUI_FADER_WIDTH;
    height = APC_GUI_FADER_HEIGHT;
    y = 0;

    if (is_master) {
        // Master fader on the right with some spacing
        x = APC_MINI_TRACK_FADER_COUNT * (width + APC_GUI_PAD_SPACING) + 10;
    } else {
        x = fader_index * (width + APC_GUI_PAD_SPACING);
    }

    return BRect(x, y, x + width - 1, y + height - 1);
}

// ===============================
// ControlButton Implementation
// ===============================

ControlButton::ControlButton(BRect frame, uint8_t button_index, ButtonType type)
    : BView(frame, "control_button", B_FOLLOW_NONE, B_WILL_DRAW | B_FRAME_EVENTS)
    , button_index(button_index)
    , button_type(type)
    , is_pressed(false)
    , led_on(false)
    , mouse_down(false)
{
    SetViewColor(B_TRANSPARENT_COLOR);
}

ControlButton::~ControlButton()
{
}

void ControlButton::Draw(BRect /*updateRect*/)
{
    BRect bounds = Bounds();
    rgb_color button_color = GetButtonColor();

    // Draw outer drop shadow for depth (like real hardware)
    SetHighColor(APC_GUI_BUTTON_SHADOW);
    BRect shadow_rect = bounds;
    shadow_rect.OffsetBy(2, 2);
    FillRect(shadow_rect);

    // Create main button area
    BRect button_rect = bounds;
    if (is_pressed) {
        button_rect.OffsetBy(1, 1);  // Pressed effect - button moves down/right
    }

    // Draw button with realistic gradient
    DrawButtonGradient(button_rect, button_color);

    // Draw 3D beveled edges for realistic hardware appearance
    if (!is_pressed) {
        // Raised button - light edges on top/left, dark on bottom/right
        SetHighColor(APC_GUI_BUTTON_HIGHLIGHT);
        // Top edge
        StrokeLine(BPoint(button_rect.left, button_rect.top),
                  BPoint(button_rect.right-1, button_rect.top));
        // Left edge
        StrokeLine(BPoint(button_rect.left, button_rect.top),
                  BPoint(button_rect.left, button_rect.bottom-1));

        SetHighColor(APC_GUI_BUTTON_SHADOW);
        // Bottom edge
        StrokeLine(BPoint(button_rect.left+1, button_rect.bottom),
                  BPoint(button_rect.right, button_rect.bottom));
        // Right edge
        StrokeLine(BPoint(button_rect.right, button_rect.top+1),
                  BPoint(button_rect.right, button_rect.bottom));
    } else {
        // Pressed button - invert the bevel effect
        SetHighColor(APC_GUI_BUTTON_SHADOW);
        // Top edge - darker when pressed
        StrokeLine(BPoint(button_rect.left, button_rect.top),
                  BPoint(button_rect.right-1, button_rect.top));
        // Left edge
        StrokeLine(BPoint(button_rect.left, button_rect.top),
                  BPoint(button_rect.left, button_rect.bottom-1));

        SetHighColor(APC_GUI_BUTTON_HIGHLIGHT);
        // Bottom edge - lighter when pressed
        StrokeLine(BPoint(button_rect.left+1, button_rect.bottom),
                  BPoint(button_rect.right, button_rect.bottom));
        // Right edge
        StrokeLine(BPoint(button_rect.right, button_rect.top+1),
                  BPoint(button_rect.right, button_rect.bottom));
    }

    // Draw main button border
    SetHighColor(APC_GUI_BUTTON_BORDER);
    StrokeRect(button_rect);

    // Add subtle surface shine effect for realism
    if (!is_pressed && (led_on || button_type == BUTTON_SHIFT)) {
        SetHighColor(255, 255, 255, 80);
        BRect shine = button_rect;
        shine.InsetBy(2, 2);
        shine.bottom = shine.top + 3;  // Small highlight at top
        FillRect(shine);
    }

    // Draw LED indicator if button has one
    if (button_type != BUTTON_SHIFT) {
        DrawLEDIndicator(button_rect);
    }

    // Draw button label with appropriate style
    DrawButtonLabel(button_rect);
}

void ControlButton::DrawButtonGradient(BRect rect, rgb_color base_color)
{
    rgb_color top_color = base_color;
    rgb_color bottom_color = base_color;

    if (!is_pressed) {
        // Lighter at top for 3D effect
        top_color.red = min_c(255, base_color.red + 30);
        top_color.green = min_c(255, base_color.green + 30);
        top_color.blue = min_c(255, base_color.blue + 30);

        bottom_color.red = max_c(0, base_color.red - 20);
        bottom_color.green = max_c(0, base_color.green - 20);
        bottom_color.blue = max_c(0, base_color.blue - 20);
    }

    // Draw gradient
    for (float y = rect.top; y <= rect.bottom; y++) {
        float ratio = (y - rect.top) / rect.Height();
        rgb_color line_color;
        line_color.red = (uint8_t)(top_color.red * (1.0f - ratio) + bottom_color.red * ratio);
        line_color.green = (uint8_t)(top_color.green * (1.0f - ratio) + bottom_color.green * ratio);
        line_color.blue = (uint8_t)(top_color.blue * (1.0f - ratio) + bottom_color.blue * ratio);
        line_color.alpha = 255;

        SetHighColor(line_color);
        StrokeLine(BPoint(rect.left, y), BPoint(rect.right, y));
    }
}

void ControlButton::DrawButtonLabel(BRect rect)
{
    // Choose appropriate text color based on button state and type
    rgb_color text_color;
    if (led_on || is_pressed) {
        text_color = rgb_color{255, 255, 255, 255};  // White when active
    } else {
        text_color = APC_GUI_LABEL_COLOR;  // Gray when inactive
    }

    SetHighColor(text_color);

    BString label_text;
    float font_size = 8;
    bool use_bold = false;

    switch (button_type) {
        case BUTTON_TRACK:
            label_text << (button_index + 1);
            font_size = 11;  // Increased from 10 for better visibility
            use_bold = true;
            break;
        case BUTTON_SCENE:
            label_text << (button_index + 1);
            font_size = 11;  // Increased from 10 for better visibility
            use_bold = true;
            break;
        case BUTTON_SHIFT:
            label_text = "SHIFT";
            font_size = 7;
            break;
    }

    // Use bold font for numbered buttons
    if (use_bold) {
        BFont font;
        GetFont(&font);
        font.SetFace(B_BOLD_FACE);
        SetFont(&font);
    }

    SetFontSize(font_size);
    float string_width = StringWidth(label_text.String());
    BPoint label_point(rect.left + rect.Width() / 2 - string_width / 2,
                      rect.top + rect.Height() / 2 + 3);
    DrawString(label_text.String(), label_point);

    // Add LED indicator for Track/Scene buttons
    if (button_type != BUTTON_SHIFT && led_on) {
        DrawLEDIndicator(rect);
    }
}

void ControlButton::DrawLEDIndicator(BRect rect)
{
    // Small LED dot in corner
    BRect led_rect(rect.right - 6, rect.top + 2, rect.right - 2, rect.top + 6);

    rgb_color led_color;
    switch (button_type) {
        case BUTTON_TRACK:
            led_color = APC_GUI_TRACK_BUTTON_ON;
            break;
        case BUTTON_SCENE:
            led_color = APC_GUI_SCENE_BUTTON_ON;
            break;
        default:
            led_color = rgb_color{255, 255, 255, 255};
            break;
    }

    SetHighColor(led_color);
    FillEllipse(led_rect);

    // LED border
    SetHighColor(APC_GUI_PAD_BORDER_COLOR);
    StrokeEllipse(led_rect);
}

void ControlButton::MouseDown(BPoint /*where*/)
{
    mouse_down = true;
    SetPressed(true);
    SendButtonMessage();
}

void ControlButton::MouseUp(BPoint /*where*/)
{
    if (mouse_down) {
        mouse_down = false;
        if (button_type != BUTTON_SHIFT) {
            SetPressed(false);
        }

        // Send button release message
        BMessage msg;
        switch (button_type) {
            case BUTTON_TRACK:
                msg.what = MSG_TRACK_BUTTON;
                break;
            case BUTTON_SCENE:
                msg.what = MSG_SCENE_BUTTON;
                break;
            case BUTTON_SHIFT:
                msg.what = MSG_SHIFT_BUTTON;
                break;
        }
        msg.AddInt32("button_index", button_index);
        msg.AddBool("pressed", false);
        Window()->PostMessage(&msg);
    }
}

void ControlButton::SetPressed(bool pressed)
{
    if (is_pressed != pressed) {
        is_pressed = pressed;
        Invalidate();
    }
}

void ControlButton::SetLEDOn(bool on)
{
    if (led_on != on) {
        led_on = on;
        Invalidate();
    }
}

rgb_color ControlButton::GetButtonColor()
{
    rgb_color base_color;

    switch (button_type) {
        case BUTTON_TRACK:
            base_color = led_on ? APC_GUI_TRACK_BUTTON_ON : APC_GUI_BUTTON_OFF_COLOR;
            break;
        case BUTTON_SCENE:
            base_color = led_on ? APC_GUI_SCENE_BUTTON_ON : APC_GUI_BUTTON_OFF_COLOR;
            break;
        case BUTTON_SHIFT:
            base_color = is_pressed ? rgb_color{255, 255, 0, 255} : APC_GUI_BUTTON_OFF_COLOR;
            break;
        default:
            base_color = APC_GUI_BUTTON_OFF_COLOR;
            break;
    }

    // Brighten if pressed
    if (is_pressed && button_type != BUTTON_SHIFT) {
        base_color.red = min_c(255, base_color.red + 50);
        base_color.green = min_c(255, base_color.green + 50);
        base_color.blue = min_c(255, base_color.blue + 50);
    }

    return base_color;
}

void ControlButton::SendButtonMessage()
{
    BMessage msg;
    switch (button_type) {
        case BUTTON_TRACK:
            msg.what = MSG_TRACK_BUTTON;
            break;
        case BUTTON_SCENE:
            msg.what = MSG_SCENE_BUTTON;
            break;
        case BUTTON_SHIFT:
            msg.what = MSG_SHIFT_BUTTON;
            break;
    }
    msg.AddInt32("button_index", button_index);
    msg.AddBool("pressed", is_pressed);
    Window()->PostMessage(&msg);
}

// ===============================
// ControlButtonView Implementation
// ===============================

ControlButtonView::ControlButtonView(BRect frame)
    : BView(frame, "control_button_view", B_FOLLOW_NONE, B_WILL_DRAW)
    , shift_button(nullptr)
{
    SetViewColor(APC_GUI_DEVICE_BODY_COLOR);
    InitializeButtons();

    // Calculate size for scene buttons column + shift button
    // Width: scene button width + minimal padding
    float button_panel_width = APC_GUI_BUTTON_WIDTH + 4;
    // Height: 8 scene buttons (aligned with pad matrix) + shift button below
    float pad_matrix_height = 8 * APC_GUI_PAD_SIZE + 7 * APC_GUI_PAD_SPACING;
    float button_panel_height = pad_matrix_height + 8 + APC_GUI_SHIFT_BUTTON_SIZE;  // Pad matrix + gap + shift

    SetExplicitMinSize(BSize(button_panel_width, button_panel_height));
    SetExplicitMaxSize(BSize(button_panel_width, button_panel_height));
    SetExplicitPreferredSize(BSize(button_panel_width, button_panel_height));
}

ControlButtonView::~ControlButtonView()
{
    // Buttons are automatically deleted as child views
}

void ControlButtonView::Draw(BRect updateRect)
{
    // Draw background
    SetHighColor(APC_GUI_BACKGROUND_COLOR);
    FillRect(updateRect);
}

void ControlButtonView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_TRACK_BUTTON:
        case MSG_SCENE_BUTTON:
        case MSG_SHIFT_BUTTON:
            // Forward to parent window
            Window()->PostMessage(message);
            break;
        default:
            BView::MessageReceived(message);
            break;
    }
}

void ControlButtonView::SetTrackButtonLED(uint8_t button_index, bool on)
{
    // Track buttons are now handled by the main window, not this view
    // This method is kept for compatibility but does nothing
    (void)button_index;
    (void)on;
}

void ControlButtonView::SetSceneButtonLED(uint8_t button_index, bool on)
{
    if (button_index < 8) {
        scene_buttons[button_index]->SetLEDOn(on);
    }
}

void ControlButtonView::SetShiftButtonPressed(bool pressed)
{
    if (shift_button) {
        shift_button->SetPressed(pressed);
    }
}

void ControlButtonView::InitializeButtons()
{
    // Only scene buttons (vertical column) and shift button
    // Track buttons are now handled separately in the main window

    // Scene buttons (vertical column)
    for (int i = 0; i < 8; i++) {
        BRect button_frame = CalculateButtonFrame(i, ControlButton::BUTTON_SCENE);
        scene_buttons[i] = new ControlButton(button_frame, i, ControlButton::BUTTON_SCENE);
        AddChild(scene_buttons[i]);
    }

    // Shift button (below scene buttons)
    BRect shift_frame = CalculateButtonFrame(0, ControlButton::BUTTON_SHIFT);
    shift_button = new ControlButton(shift_frame, 0, ControlButton::BUTTON_SHIFT);
    AddChild(shift_button);
}

BRect ControlButtonView::CalculateButtonFrame(uint8_t index, ControlButton::ButtonType type)
{
    float width, height;
    float x, y;

    switch (type) {
        case ControlButton::BUTTON_SCENE:
        {
            // Vertical column of scene buttons - align with pad rows
            width = APC_GUI_BUTTON_WIDTH;
            height = APC_GUI_BUTTON_HEIGHT;
            x = 3;  // Small padding from left edge
            // Space scene buttons to align with pad matrix rows
            float pad_row_spacing = APC_GUI_PAD_SIZE + APC_GUI_PAD_SPACING;
            y = index * pad_row_spacing + (APC_GUI_PAD_SIZE - APC_GUI_BUTTON_HEIGHT) / 2; // Center on pad row
            break;
        }

        case ControlButton::BUTTON_SHIFT:
        {
            // Square button below scene buttons - align with bottom of pad matrix
            width = height = APC_GUI_SHIFT_BUTTON_SIZE;
            x = 3 + (APC_GUI_BUTTON_WIDTH - APC_GUI_SHIFT_BUTTON_SIZE) / 2;  // Center under scene buttons
            float pad_matrix_height = 8 * APC_GUI_PAD_SIZE + 7 * APC_GUI_PAD_SPACING;
            y = pad_matrix_height + 10;  // Below pad matrix with gap
            break;
        }

        case ControlButton::BUTTON_TRACK:
        default:
            // Track buttons no longer handled here
            width = height = x = y = 0;
            break;
    }

    return BRect(x, y, x + width - 1, y + height - 1);
}

// ===============================
// BrandedBackgroundView Implementation
// ===============================

BrandedBackgroundView::BrandedBackgroundView(BRect frame)
    : BView(frame, "branded_background", B_FOLLOW_ALL, B_WILL_DRAW)
{
    SetViewColor(APC_GUI_DEVICE_BODY_COLOR);
}

BrandedBackgroundView::~BrandedBackgroundView()
{
}

void BrandedBackgroundView::Draw(BRect /*updateRect*/)
{
    BRect bounds = Bounds();

    // Draw the realistic device body with texture and branding
    DrawDeviceBody(bounds);
    DrawTexturedSurface(bounds);
    DrawRealisticShadows(bounds);
    DrawAKAIBranding(bounds);
    DrawModelLabels(bounds);
}

void BrandedBackgroundView::DrawDeviceBody(BRect bounds)
{
    // Fill with base device color
    SetHighColor(APC_GUI_DEVICE_BODY_COLOR);
    FillRect(bounds);

    // Add subtle gradient for depth
    rgb_color top_color = APC_GUI_DEVICE_BODY_HIGHLIGHT;
    rgb_color bottom_color = APC_GUI_DEVICE_BODY_SHADOW;

    // Very subtle gradient to simulate matte plastic surface
    for (float y = bounds.top; y <= bounds.bottom; y += 2) {
        float ratio = (y - bounds.top) / bounds.Height();
        rgb_color line_color;
        line_color.red = (uint8_t)(top_color.red * (1.0f - ratio) + bottom_color.red * ratio);
        line_color.green = (uint8_t)(top_color.green * (1.0f - ratio) + bottom_color.green * ratio);
        line_color.blue = (uint8_t)(top_color.blue * (1.0f - ratio) + bottom_color.blue * ratio);
        line_color.alpha = 255;

        SetHighColor(line_color);
        StrokeLine(BPoint(bounds.left, y), BPoint(bounds.right, y));
    }
}

void BrandedBackgroundView::DrawAKAIBranding(BRect bounds)
{
    // Draw AKAI logo/text in the top-right area like real hardware
    SetHighColor(APC_GUI_BRAND_COLOR);

    // Use bold font for AKAI branding
    BFont font;
    GetFont(&font);
    font.SetFace(B_BOLD_FACE);
    SetFont(&font);

    SetFontSize(16);  // Increased from 14 for better visibility
    font_height fh;
    GetFontHeight(&fh);

    BString akai_text = "AKAI";
    float text_width = StringWidth(akai_text.String());
    BPoint akai_pos(bounds.right - text_width - 25, bounds.top + 35);
    DrawString(akai_text.String(), akai_pos);

    // Add "professional" underneath in smaller text
    font.SetFace(B_REGULAR_FACE);
    SetFont(&font);
    SetFontSize(9);  // Increased from 8 for better readability
    BString pro_text = "professional";
    float pro_width = StringWidth(pro_text.String());
    BPoint pro_pos(bounds.right - pro_width - 25, akai_pos.y + fh.ascent + fh.descent + 3);
    DrawString(pro_text.String(), pro_pos);
}

void BrandedBackgroundView::DrawModelLabels(BRect bounds)
{
    // Draw "APC mini mk2" model label like real hardware
    SetHighColor(APC_GUI_LABEL_COLOR);
    SetFontSize(10);

    BString model_text = "APC mini mk2";
    BPoint model_pos(bounds.left + 20, bounds.bottom - 40);
    DrawString(model_text.String(), model_pos);

    // Add smaller labels for sections like real hardware
    SetFontSize(7);
    SetHighColor(APC_GUI_LABEL_COLOR);

    // "TRACK SELECT" label above track buttons area
    BString track_label = "TRACK SELECT";
    BPoint track_pos(20, 45);  // Above track buttons
    DrawString(track_label.String(), track_pos);

    // "SCENE LAUNCH" label next to scene buttons
    BString scene_label = "SCENE LAUNCH";
    float scene_width = StringWidth(scene_label.String());
    // Position this vertically along the scene buttons
    BPoint scene_pos(bounds.right - scene_width - 60, bounds.Height() / 2);
    DrawString(scene_label.String(), scene_pos);

    // "CLIP/DEVICE CONTROL" label for the pad matrix
    SetFontSize(8);
    BString clip_label = "CLIP/DEVICE CONTROL";
    BPoint clip_pos(30, 75);  // Above pad matrix
    DrawString(clip_label.String(), clip_pos);
}

void BrandedBackgroundView::DrawTexturedSurface(BRect bounds)
{
    // Add very subtle texture to simulate matte plastic finish
    SetHighColor(APC_GUI_SURFACE_SHINE);

    // Draw subtle dots pattern for texture (very faint)
    for (int x = bounds.left; x < bounds.right; x += 8) {
        for (int y = bounds.top; y < bounds.bottom; y += 8) {
            if ((x + y) % 16 == 0) {  // Sparse pattern
                SetHighColor(255, 255, 255, 15);  // Very faint white dots
                FillRect(BRect(x, y, x, y));
            }
        }
    }
}

void BrandedBackgroundView::DrawRealisticShadows(BRect bounds)
{
    // Add subtle inner shadows around the edges for depth
    SetHighColor(APC_GUI_BEVEL_DARK);

    // Top shadow
    StrokeLine(BPoint(bounds.left, bounds.top),
              BPoint(bounds.right, bounds.top));

    // Left shadow
    StrokeLine(BPoint(bounds.left, bounds.top),
              BPoint(bounds.left, bounds.bottom));

    // Add very subtle highlights on opposite edges
    SetHighColor(APC_GUI_BEVEL_LIGHT);

    // Bottom highlight
    StrokeLine(BPoint(bounds.left + 1, bounds.bottom),
              BPoint(bounds.right, bounds.bottom));

    // Right highlight
    StrokeLine(BPoint(bounds.right, bounds.top + 1),
              BPoint(bounds.right, bounds.bottom));
}