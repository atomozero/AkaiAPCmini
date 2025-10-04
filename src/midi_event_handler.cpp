/*
 * MIDI Event Handler Implementation
 */

#include "midi_event_handler.h"
#include <algorithm>
#include <chrono>

// MIDIEventFilter implementation
bool MIDIEventFilter::ShouldAccept(const MIDIMessage& msg) const
{
    // Check message type
    uint8_t status = msg.status & 0xF0;
    switch (status) {
        case 0x90: // Note On
            if (!accept_note_on) return false;
            if (msg.data2 < min_velocity || msg.data2 > max_velocity) return false;
            break;
        case 0x80: // Note Off
            if (!accept_note_off) return false;
            break;
        case 0xB0: // Control Change
            if (!accept_cc) return false;
            break;
        case 0xF0: // SysEx
            if (!accept_sysex) return false;
            break;
    }

    // Check source
    switch (msg.source) {
        case MIDI_SOURCE_HARDWARE_USB:
        case MIDI_SOURCE_HARDWARE_MIDI:
            if (!accept_from_hardware) return false;
            break;
        case MIDI_SOURCE_GUI:
            if (!accept_from_gui) return false;
            break;
    }

    return true;
}

// MIDIEventHandler implementation
MIDIEventHandler::MIDIEventHandler(const char* name)
    : BHandler(name)
    , message_queue(nullptr)
{
    // Initialize priority map with defaults
    for (int i = 0; i < 128; i++) {
        priority_map[i] = GetDefaultPriority(i);
    }
}

MIDIEventHandler::~MIDIEventHandler()
{
    callbacks.clear();
}

void MIDIEventHandler::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_MIDI_EVENT:
        {
            // Extract MIDI message from BMessage
            int32 status, data1, data2, source;
            if (message->FindInt32("status", &status) == B_OK &&
                message->FindInt32("data1", &data1) == B_OK &&
                message->FindInt32("data2", &data2) == B_OK &&
                message->FindInt32("source", &source) == B_OK) {

                MIDIMessage midi_msg;
                midi_msg.status = status;
                midi_msg.data1 = data1;
                midi_msg.data2 = data2;
                midi_msg.source = static_cast<MIDIMessageSource>(source);
                midi_msg.timestamp = system_time();

                ProcessMessage(midi_msg);
            }
            break;
        }

        case MSG_PROCESS_QUEUE:
        {
            ProcessPendingEvents();
            break;
        }

        case MSG_UPDATE_METRICS:
        {
            if (message_queue) {
                metrics.current_queue_depth = message_queue->GetQueueDepth();
            }
            break;
        }

        default:
            BHandler::MessageReceived(message);
            break;
    }
}

bool MIDIEventHandler::SubmitEvent(uint8_t status, uint8_t data1, uint8_t data2,
                                   MIDIMessageSource source)
{
    if (!message_queue) return false;

    MIDIMessage msg;
    msg.status = status;
    msg.data1 = data1;
    msg.data2 = data2;
    msg.source = source;
    msg.timestamp = system_time();
    msg.priority = GetEventPriority(status & 0xF0);

    return message_queue->Enqueue(msg);
}

bool MIDIEventHandler::SubmitEvent(const MIDIMessage& message)
{
    if (!message_queue) return false;
    return message_queue->Enqueue(message);
}

bool MIDIEventHandler::SubmitSysEx(const uint8_t* data, size_t length,
                                   MIDIMessageSource source)
{
    if (!message_queue || !data || length == 0) return false;

    // For SysEx, we store the first few bytes in the message
    // and handle the full data separately if needed
    MIDIMessage msg;
    msg.status = 0xF0; // SysEx start
    msg.data1 = (length > 1) ? data[1] : 0;
    msg.data2 = (length > 2) ? data[2] : 0;
    msg.source = source;
    msg.timestamp = system_time();
    msg.priority = MIDI_PRIORITY_LOW;
    msg.sysex_length = length;

    return message_queue->Enqueue(msg);
}

MIDIEventHandler::CallbackID MIDIEventHandler::RegisterCallback(
    MIDIEventCallback callback, const MIDIEventFilter& filter)
{
    if (callbacks.size() >= MAX_CALLBACKS) {
        return 0; // Maximum callbacks reached
    }

    CallbackEntry entry;
    entry.id = next_callback_id.fetch_add(1);
    entry.callback = callback;
    entry.filter = filter;
    entry.enabled = true;

    callbacks.push_back(entry);
    return entry.id;
}

void MIDIEventHandler::UnregisterCallback(CallbackID id)
{
    auto it = std::find_if(callbacks.begin(), callbacks.end(),
        [id](const CallbackEntry& entry) { return entry.id == id; });

    if (it != callbacks.end()) {
        callbacks.erase(it);
    }
}

void MIDIEventHandler::SetCallbackEnabled(CallbackID id, bool enabled)
{
    auto it = std::find_if(callbacks.begin(), callbacks.end(),
        [id](const CallbackEntry& entry) { return entry.id == id; });

    if (it != callbacks.end()) {
        it->enabled = enabled;
    }
}

void MIDIEventHandler::ProcessPendingEvents()
{
    if (!message_queue) return;

    MIDIMessage message;
    int processed = 0;
    const int max_batch = 32; // Process max 32 messages per call to maintain responsiveness

    while (processed < max_batch && message_queue->Dequeue(message)) {
        ProcessMessage(message);
        processed++;
    }

    metrics.current_queue_depth = message_queue->GetQueueDepth();
}

void MIDIEventHandler::ProcessSingleEvent(const MIDIMessage& message)
{
    ProcessMessage(message);
}

void MIDIEventHandler::ProcessMessage(const MIDIMessage& message)
{
    bigtime_t start_time = system_time();

    // Check if we should process this message
    if (!ShouldProcessMessage(message)) {
        metrics.events_filtered.fetch_add(1);
        return;
    }

    // Execute callbacks
    ExecuteCallbacks(message);

    // Update metrics
    bigtime_t processing_time = system_time() - start_time;
    UpdateMetrics(message, processing_time);

    metrics.events_processed.fetch_add(1);
}

void MIDIEventHandler::ExecuteCallbacks(const MIDIMessage& message)
{
    for (const auto& entry : callbacks) {
        if (entry.enabled && entry.filter.ShouldAccept(message)) {
            try {
                entry.callback(message);
                metrics.callbacks_executed.fetch_add(1);
            } catch (...) {
                // Protect against callback exceptions
                // In production, log this error
            }
        }
    }
}

bool MIDIEventHandler::ShouldProcessMessage(const MIDIMessage& message) const
{
    // Apply global filter
    if (!global_filter.ShouldAccept(message)) {
        return false;
    }

    // Feedback prevention
    if (prevent_feedback && message.source == MIDI_SOURCE_GUI) {
        uint32_t current_time = system_time() / 1000; // Convert to ms
        uint32_t last_time = last_gui_message_time.load();

        if (current_time - last_time < FEEDBACK_PREVENTION_WINDOW_MS) {
            return false; // Too soon, might be feedback
        }
    }

    return true;
}

void MIDIEventHandler::UpdateMetrics(const MIDIMessage& message, bigtime_t processing_time)
{
    uint32_t time_us = processing_time;

    // Update max processing time
    uint32_t current_max = metrics.max_processing_time_us.load();
    while (time_us > current_max) {
        if (metrics.max_processing_time_us.compare_exchange_weak(current_max, time_us)) {
            break;
        }
    }

    // Update average (simple moving average)
    uint32_t current_avg = metrics.avg_processing_time_us.load();
    uint32_t new_avg = (current_avg * 7 + time_us) / 8; // Weight towards history
    metrics.avg_processing_time_us.store(new_avg);

    // Update last GUI message time for feedback prevention
    if (message.source == MIDI_SOURCE_GUI) {
        last_gui_message_time.store(system_time() / 1000);
    }
}

void MIDIEventHandler::SetGlobalFilter(const MIDIEventFilter& filter)
{
    global_filter = filter;
}

MIDIEventMetricsSnapshot MIDIEventHandler::GetMetrics() const
{
    MIDIEventMetricsSnapshot snapshot;
    snapshot.events_processed = metrics.events_processed.load();
    snapshot.events_filtered = metrics.events_filtered.load();
    snapshot.callbacks_executed = metrics.callbacks_executed.load();
    snapshot.max_processing_time_us = metrics.max_processing_time_us.load();
    snapshot.avg_processing_time_us = metrics.avg_processing_time_us.load();
    snapshot.current_queue_depth = metrics.current_queue_depth.load();
    return snapshot;
}

void MIDIEventHandler::SetEventPriority(uint8_t message_type, MIDIEventPriority priority)
{
    if (message_type < 128) {
        priority_map[message_type] = priority;
    }
}

MIDIEventPriority MIDIEventHandler::GetEventPriority(uint8_t message_type) const
{
    if (message_type < 128) {
        return static_cast<MIDIEventPriority>(priority_map[message_type]);
    }
    return MIDI_PRIORITY_NORMAL;
}

const char* MIDIEventHandler::GetSourceName(MIDIMessageSource source)
{
    switch (source) {
        case MIDI_SOURCE_HARDWARE_USB: return "Hardware USB";
        case MIDI_SOURCE_HARDWARE_MIDI: return "Hardware MIDI";
        case MIDI_SOURCE_GUI: return "GUI";
        case MIDI_SOURCE_SIMULATION: return "Simulation";
        default: return "Unknown";
    }
}

const char* MIDIEventHandler::GetMessageTypeName(uint8_t status)
{
    switch (status & 0xF0) {
        case 0x80: return "Note Off";
        case 0x90: return "Note On";
        case 0xA0: return "Aftertouch";
        case 0xB0: return "Control Change";
        case 0xC0: return "Program Change";
        case 0xD0: return "Channel Pressure";
        case 0xE0: return "Pitch Bend";
        case 0xF0: return "System";
        default: return "Unknown";
    }
}

MIDIEventPriority MIDIEventHandler::GetDefaultPriority(uint8_t status)
{
    switch (status & 0xF0) {
        case 0x80: // Note Off
        case 0x90: // Note On
            return MIDI_PRIORITY_HIGH;
        case 0xB0: // Control Change
            return MIDI_PRIORITY_NORMAL;
        case 0xF0: // System messages
            if (status >= 0xF8) { // Real-time messages
                return MIDI_PRIORITY_REALTIME;
            }
            return MIDI_PRIORITY_LOW; // SysEx
        default:
            return MIDI_PRIORITY_NORMAL;
    }
}

// MIDIEventLooper implementation
MIDIEventLooper::MIDIEventLooper(MIDIEventHandler* handler, const char* name, int32 priority)
    : BLooper(name, priority)
    , event_handler(handler)
    , processing_thread(-1)
{
    if (event_handler) {
        AddHandler(event_handler);
    }
}

MIDIEventLooper::~MIDIEventLooper()
{
    StopProcessing();
    if (event_handler) {
        RemoveHandler(event_handler);
    }
}

void MIDIEventLooper::StartProcessing()
{
    if (is_processing) return;

    should_quit = false;
    is_processing = true;

    // Start the processing thread
    processing_thread = spawn_thread(ProcessingThread, "MIDI Processing",
                                    B_REAL_TIME_DISPLAY_PRIORITY, this);

    if (processing_thread >= 0) {
        resume_thread(processing_thread);
    } else {
        is_processing = false;
    }
}

void MIDIEventLooper::StopProcessing()
{
    if (!is_processing) return;

    should_quit = true;
    is_processing = false;

    if (processing_thread >= 0) {
        status_t result;
        wait_for_thread(processing_thread, &result);
        processing_thread = -1;
    }
}

void MIDIEventLooper::SetPollingInterval(uint32_t microseconds)
{
    polling_interval_us = microseconds;
}

void MIDIEventLooper::MessageReceived(BMessage* message)
{
    // Forward to the event handler
    if (event_handler) {
        event_handler->MessageReceived(message);
    } else {
        BLooper::MessageReceived(message);
    }
}

bool MIDIEventLooper::QuitRequested()
{
    StopProcessing();
    return BLooper::QuitRequested();
}

int32 MIDIEventLooper::ProcessingThread(void* data)
{
    MIDIEventLooper* looper = static_cast<MIDIEventLooper*>(data);
    looper->ProcessingLoop();
    return 0;
}

void MIDIEventLooper::ProcessingLoop()
{
    while (!should_quit) {
        // Process pending MIDI events
        if (event_handler) {
            event_handler->ProcessPendingEvents();
        }

        // Sleep for the polling interval
        snooze(polling_interval_us.load());
    }
}