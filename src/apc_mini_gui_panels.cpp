// apc_mini_gui_panels.cpp
// Implementation of diagnostic and performance panels for APC Mini GUI
//
// ConnectionStatusPanel: Shows USB Raw vs MIDI fallback connection status
// PerformanceIndicatorPanel: Shows real-time latency and message statistics

#include "apc_mini_gui.h"
#include <LayoutBuilder.h>
#include <GroupLayoutBuilder.h>
#include <StringFormat.h>
#include <stdio.h>

// ============================================================================
// ConnectionStatusPanel Implementation
// ============================================================================

ConnectionStatusPanel::ConnectionStatusPanel(BRect frame)
    : BView(frame, "ConnectionStatus", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP, B_WILL_DRAW | B_PULSE_NEEDED)
    , is_usb_connected(false)
    , is_midi_fallback(false)
    , device_name("Not Connected")
    , vendor_id(0)
    , product_id(0)
{
    SetViewColor(APC_GUI_BACKGROUND_COLOR);

    // Create labels
    connection_label = new BStringView("connection", "Connection: Initializing...");
    mode_label = new BStringView("mode", "Mode: ---");
    device_label = new BStringView("device", "Device: ---");
    performance_label = new BStringView("perf", "Status: Waiting for device");

    // Set label colors
    connection_label->SetHighColor(APC_GUI_TEXT_COLOR);
    mode_label->SetHighColor(APC_GUI_LABEL_COLOR);
    device_label->SetHighColor(APC_GUI_LABEL_COLOR);
    performance_label->SetHighColor(APC_GUI_LABEL_COLOR);

    // Set font to be smaller for compact display
    BFont font(be_plain_font);
    font.SetSize(10.0);
    connection_label->SetFont(&font);
    mode_label->SetFont(&font);
    device_label->SetFont(&font);
    performance_label->SetFont(&font);

    // Layout
    BLayoutBuilder::Group<>(this, B_VERTICAL, 2)
        .SetInsets(5, 5, 5, 5)
        .Add(connection_label)
        .Add(mode_label)
        .Add(device_label)
        .Add(performance_label)
    .End();
}

ConnectionStatusPanel::~ConnectionStatusPanel()
{
}

void ConnectionStatusPanel::AttachedToWindow()
{
    BView::AttachedToWindow();
    SetPulseRate(500000); // Update every 500ms
}

void ConnectionStatusPanel::Pulse()
{
    // Pulse updates handled by explicit UpdateStatus() calls from app
}

void ConnectionStatusPanel::Draw(BRect updateRect)
{
    BView::Draw(updateRect);

    // Draw border
    SetHighColor(APC_GUI_DEVICE_BODY_HIGHLIGHT);
    StrokeRect(Bounds());
}

void ConnectionStatusPanel::UpdateStatus(bool usb_connected, bool midi_fallback)
{
    is_usb_connected = usb_connected;
    is_midi_fallback = midi_fallback;
    UpdateLabels();
}

void ConnectionStatusPanel::SetDeviceInfo(const char* name, uint16_t vid, uint16_t pid)
{
    device_name = name;
    vendor_id = vid;
    product_id = pid;
    UpdateLabels();
}

void ConnectionStatusPanel::UpdateLabels()
{
    BString conn_text, mode_text, device_text, perf_text;

    if (is_usb_connected) {
        conn_text = "Connection: ✓ Connected";
        connection_label->SetHighColor(rgb_color{50, 255, 50, 255}); // Green

        if (is_midi_fallback) {
            mode_text = "Mode: MIDI Fallback (Hardware issues detected)";
            mode_label->SetHighColor(rgb_color{255, 200, 50, 255}); // Yellow warning
            perf_text = "Performance: Degraded (~5ms latency)";
            performance_label->SetHighColor(rgb_color{255, 150, 50, 255}); // Orange
        } else {
            mode_text = "Mode: USB Raw Access (Optimal)";
            mode_label->SetHighColor(rgb_color{50, 255, 50, 255}); // Green
            perf_text = "Performance: Excellent (<1ms latency)";
            performance_label->SetHighColor(rgb_color{50, 255, 50, 255}); // Green
        }

        if (vendor_id != 0 && product_id != 0) {
            device_text.SetToFormat("Device: %s (VID:0x%04X PID:0x%04X)",
                                   device_name.String(), vendor_id, product_id);
        } else {
            device_text.SetToFormat("Device: %s", device_name.String());
        }
        device_label->SetHighColor(APC_GUI_TEXT_COLOR);

    } else {
        conn_text = "Connection: ✗ Disconnected";
        connection_label->SetHighColor(rgb_color{255, 50, 50, 255}); // Red
        mode_text = "Mode: ---";
        mode_label->SetHighColor(APC_GUI_LABEL_COLOR);
        device_text = "Device: Not detected";
        device_label->SetHighColor(APC_GUI_LABEL_COLOR);
        perf_text = "Status: No device connected";
        performance_label->SetHighColor(APC_GUI_LABEL_COLOR);
    }

    if (LockLooper()) {
        connection_label->SetText(conn_text);
        mode_label->SetText(mode_text);
        device_label->SetText(device_text);
        performance_label->SetText(perf_text);
        UnlockLooper();
    }
}

rgb_color ConnectionStatusPanel::GetStatusColor()
{
    if (!is_usb_connected) {
        return rgb_color{255, 50, 50, 255}; // Red
    } else if (is_midi_fallback) {
        return rgb_color{255, 200, 50, 255}; // Yellow
    } else {
        return rgb_color{50, 255, 50, 255}; // Green
    }
}

// ============================================================================
// PerformanceIndicatorPanel Implementation
// ============================================================================

PerformanceIndicatorPanel::PerformanceIndicatorPanel(BRect frame)
    : BView(frame, "PerformanceIndicator", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP, B_WILL_DRAW | B_PULSE_NEEDED)
    , min_latency_us(UINT64_MAX)
    , max_latency_us(0)
    , total_latency_us(0)
    , latency_samples(0)
    , messages_sent(0)
    , messages_received(0)
    , last_update_time(0)
    , current_latency_us(0.0)
    , avg_latency_us(0.0)
    , current_throughput(0.0)
{
    SetViewColor(APC_GUI_BACKGROUND_COLOR);

    // Create labels
    latency_label = new BStringView("latency", "Latency: --- μs");
    messages_label = new BStringView("messages", "Messages: TX:0 RX:0");
    throughput_label = new BStringView("throughput", "Throughput: --- msg/s");

    // Set label colors
    latency_label->SetHighColor(APC_GUI_TEXT_COLOR);
    messages_label->SetHighColor(APC_GUI_LABEL_COLOR);
    throughput_label->SetHighColor(APC_GUI_LABEL_COLOR);

    // Set font
    BFont font(be_plain_font);
    font.SetSize(10.0);
    latency_label->SetFont(&font);
    messages_label->SetFont(&font);
    throughput_label->SetFont(&font);

    // Layout
    BLayoutBuilder::Group<>(this, B_VERTICAL, 2)
        .SetInsets(5, 5, 5, 5)
        .Add(latency_label)
        .Add(messages_label)
        .Add(throughput_label)
    .End();

    last_update_time = system_time();
}

PerformanceIndicatorPanel::~PerformanceIndicatorPanel()
{
}

void PerformanceIndicatorPanel::AttachedToWindow()
{
    BView::AttachedToWindow();
    SetPulseRate(100000); // Update every 100ms
}

void PerformanceIndicatorPanel::Pulse()
{
    CalculateStatistics();
    UpdateLabels();
}

void PerformanceIndicatorPanel::Draw(BRect updateRect)
{
    BView::Draw(updateRect);

    // Draw border
    SetHighColor(APC_GUI_DEVICE_BODY_HIGHLIGHT);
    StrokeRect(Bounds());
}

void PerformanceIndicatorPanel::RecordLatency(bigtime_t latency_us)
{
    if (latency_us < min_latency_us) min_latency_us = latency_us;
    if (latency_us > max_latency_us) max_latency_us = latency_us;
    total_latency_us += latency_us;
    latency_samples++;
}

void PerformanceIndicatorPanel::IncrementMessageCount(bool sent)
{
    if (sent) {
        messages_sent++;
    } else {
        messages_received++;
    }
}

void PerformanceIndicatorPanel::ResetStatistics()
{
    min_latency_us = UINT64_MAX;
    max_latency_us = 0;
    total_latency_us = 0;
    latency_samples = 0;
    messages_sent = 0;
    messages_received = 0;
    last_update_time = system_time();
    current_latency_us = 0.0;
    avg_latency_us = 0.0;
    current_throughput = 0.0;
}

void PerformanceIndicatorPanel::CalculateStatistics()
{
    if (latency_samples > 0) {
        current_latency_us = max_latency_us;
        avg_latency_us = (float)total_latency_us / latency_samples;
    }

    // Calculate throughput (messages per second)
    bigtime_t current_time = system_time();
    bigtime_t elapsed = current_time - last_update_time;
    if (elapsed > 0) {
        uint32_t total_messages = messages_sent + messages_received;
        current_throughput = (float)total_messages * 1000000.0 / elapsed;
    }
}

void PerformanceIndicatorPanel::UpdateLabels()
{
    BString latency_text, messages_text, throughput_text;

    if (latency_samples > 0) {
        latency_text.SetToFormat("Latency: %.1f μs (avg: %.1f μs)",
                                 current_latency_us, avg_latency_us);
        latency_label->SetHighColor(GetLatencyColor(avg_latency_us));
    } else {
        latency_text = "Latency: --- μs";
        latency_label->SetHighColor(APC_GUI_LABEL_COLOR);
    }

    messages_text.SetToFormat("Messages: TX:%u RX:%u", messages_sent, messages_received);

    if (current_throughput > 0) {
        throughput_text.SetToFormat("Throughput: %.0f msg/s", current_throughput);
    } else {
        throughput_text = "Throughput: --- msg/s";
    }

    if (LockLooper()) {
        latency_label->SetText(latency_text);
        messages_label->SetText(messages_text);
        throughput_label->SetText(throughput_text);
        UnlockLooper();
    }
}

rgb_color PerformanceIndicatorPanel::GetLatencyColor(float latency)
{
    if (latency < 100) {
        return rgb_color{50, 255, 50, 255}; // Green - excellent (<100μs)
    } else if (latency < 1000) {
        return rgb_color{255, 200, 50, 255}; // Yellow - good (<1ms)
    } else if (latency < 5000) {
        return rgb_color{255, 150, 50, 255}; // Orange - acceptable (<5ms)
    } else {
        return rgb_color{255, 50, 50, 255}; // Red - poor (>5ms)
    }
}
