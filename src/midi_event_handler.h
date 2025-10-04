/*
 * MIDI Event Handler for APC Mini
 *
 * Centralized event handling system for MIDI messages following
 * real-time audio best practices and Haiku OS conventions.
 *
 * This handler provides:
 * - Unified interface for all MIDI events
 * - Source tracking to prevent feedback loops
 * - Priority-based event processing
 * - Real-time safe operation
 * - Integration with the lock-free message queue
 */

#ifndef MIDI_EVENT_HANDLER_H
#define MIDI_EVENT_HANDLER_H

#include <Message.h>
#include <Messenger.h>
#include <Handler.h>
#include <Looper.h>
#include <OS.h>
#include <vector>
#include <functional>
#include <atomic>

#include "midi_message_queue.h"
#include "apc_mini_defs.h"

// Event priorities for real-time scheduling
enum MIDIEventPriority {
    MIDI_PRIORITY_REALTIME = 0,    // System real-time messages (clock, start, stop)
    MIDI_PRIORITY_HIGH = 1,        // Note on/off, performance controls
    MIDI_PRIORITY_NORMAL = 2,      // CC changes, program changes
    MIDI_PRIORITY_LOW = 3,         // SysEx, non-critical updates
    MIDI_PRIORITY_GUI = 4          // GUI-originated events (lowest priority)
};

// Event callback signature
using MIDIEventCallback = std::function<void(const MIDIMessage&)>;

// Event filter for selective processing
struct MIDIEventFilter {
    bool accept_note_on = true;
    bool accept_note_off = true;
    bool accept_cc = true;
    bool accept_sysex = true;
    bool accept_from_hardware = true;
    bool accept_from_gui = true;
    uint8_t min_velocity = 0;
    uint8_t max_velocity = 127;

    bool ShouldAccept(const MIDIMessage& msg) const;
};

// Performance metrics for monitoring
struct MIDIEventMetrics {
    std::atomic<uint64_t> events_processed{0};
    std::atomic<uint64_t> events_filtered{0};
    std::atomic<uint64_t> callbacks_executed{0};
    std::atomic<uint32_t> max_processing_time_us{0};
    std::atomic<uint32_t> avg_processing_time_us{0};
    std::atomic<uint32_t> current_queue_depth{0};

    void Reset() {
        events_processed = 0;
        events_filtered = 0;
        callbacks_executed = 0;
        max_processing_time_us = 0;
        avg_processing_time_us = 0;
        current_queue_depth = 0;
    }

    // Copy values for thread-safe access
    void CopyTo(MIDIEventMetrics& dest) const {
        dest.events_processed = events_processed.load();
        dest.events_filtered = events_filtered.load();
        dest.callbacks_executed = callbacks_executed.load();
        dest.max_processing_time_us = max_processing_time_us.load();
        dest.avg_processing_time_us = avg_processing_time_us.load();
        dest.current_queue_depth = current_queue_depth.load();
    }
};

// Non-atomic version for returning metrics
struct MIDIEventMetricsSnapshot {
    uint64_t events_processed;
    uint64_t events_filtered;
    uint64_t callbacks_executed;
    uint32_t max_processing_time_us;
    uint32_t avg_processing_time_us;
    uint32_t current_queue_depth;
};

/*
 * MIDIEventHandler - Central MIDI event processing
 *
 * This class manages all MIDI event routing and processing,
 * ensuring real-time safety and proper thread synchronization.
 */
class MIDIEventHandler : public BHandler {
public:
    MIDIEventHandler(const char* name = "MIDIEventHandler");
    virtual ~MIDIEventHandler();

    // BHandler interface
    virtual void MessageReceived(BMessage* message) override;

    // Event submission (thread-safe, real-time safe)
    bool SubmitEvent(uint8_t status, uint8_t data1, uint8_t data2,
                     MIDIMessageSource source = MIDI_SOURCE_HARDWARE_USB);
    bool SubmitEvent(const MIDIMessage& message);
    bool SubmitSysEx(const uint8_t* data, size_t length,
                     MIDIMessageSource source = MIDI_SOURCE_HARDWARE_USB);

    // Callback management
    using CallbackID = uint32_t;
    CallbackID RegisterCallback(MIDIEventCallback callback,
                                const MIDIEventFilter& filter = MIDIEventFilter());
    void UnregisterCallback(CallbackID id);
    void SetCallbackEnabled(CallbackID id, bool enabled);

    // Direct event processing (for GUI thread)
    void ProcessPendingEvents();
    void ProcessSingleEvent(const MIDIMessage& message);

    // Filter management
    void SetGlobalFilter(const MIDIEventFilter& filter);
    MIDIEventFilter GetGlobalFilter() const { return global_filter; }

    // Feedback loop prevention
    void SetFeedbackPrevention(bool enabled) { prevent_feedback = enabled; }
    bool IsFeedbackPreventionEnabled() const { return prevent_feedback; }

    // Queue management
    void SetMessageQueue(MIDIMessageQueue* queue) { message_queue = queue; }
    MIDIMessageQueue* GetMessageQueue() const { return message_queue; }

    // Performance monitoring
    MIDIEventMetricsSnapshot GetMetrics() const;
    void ResetMetrics() { metrics.Reset(); }

    // Priority management
    void SetEventPriority(uint8_t message_type, MIDIEventPriority priority);
    MIDIEventPriority GetEventPriority(uint8_t message_type) const;

    // Utility methods
    static const char* GetSourceName(MIDIMessageSource source);
    static const char* GetMessageTypeName(uint8_t status);
    static MIDIEventPriority GetDefaultPriority(uint8_t status);

private:
    // Callback registration
    struct CallbackEntry {
        CallbackID id;
        MIDIEventCallback callback;
        MIDIEventFilter filter;
        bool enabled;
    };

    // Internal processing
    void ProcessMessage(const MIDIMessage& message);
    void ExecuteCallbacks(const MIDIMessage& message);
    bool ShouldProcessMessage(const MIDIMessage& message) const;
    void UpdateMetrics(const MIDIMessage& message, bigtime_t processing_time);

    // Member variables
    MIDIMessageQueue* message_queue;
    std::vector<CallbackEntry> callbacks;
    MIDIEventFilter global_filter;
    MIDIEventMetrics metrics;
    std::atomic<CallbackID> next_callback_id{1};
    std::atomic<bool> prevent_feedback{true};
    std::atomic<uint32_t> last_gui_message_time{0};

    // Priority map for different message types
    uint8_t priority_map[128];

    // Constants
    static constexpr uint32_t FEEDBACK_PREVENTION_WINDOW_MS = 50;
    static constexpr uint32_t MAX_CALLBACKS = 32;
    static constexpr uint32_t METRICS_UPDATE_INTERVAL_MS = 100;
};

/*
 * MIDIEventLooper - Dedicated looper for MIDI event processing
 *
 * Runs in a high-priority thread for timely event processing
 * while maintaining real-time safety.
 */
class MIDIEventLooper : public BLooper {
public:
    MIDIEventLooper(MIDIEventHandler* handler,
                    const char* name = "MIDIEventLooper",
                    int32 priority = B_REAL_TIME_DISPLAY_PRIORITY);
    virtual ~MIDIEventLooper();

    // Start/stop processing
    void StartProcessing();
    void StopProcessing();
    bool IsProcessing() const { return is_processing; }

    // Queue polling control
    void SetPollingInterval(uint32_t microseconds);
    uint32_t GetPollingInterval() const { return polling_interval_us; }

    // Thread control
    virtual void MessageReceived(BMessage* message) override;
    virtual bool QuitRequested() override;

private:
    static int32 ProcessingThread(void* data);
    void ProcessingLoop();

    MIDIEventHandler* event_handler;
    thread_id processing_thread;
    std::atomic<bool> is_processing{false};
    std::atomic<bool> should_quit{false};
    std::atomic<uint32_t> polling_interval_us{1000}; // 1ms default
};

// Message constants for BMessage communication
enum {
    MSG_MIDI_EVENT = 'MEvt',
    MSG_MIDI_SYSEX = 'MSEx',
    MSG_PROCESS_QUEUE = 'MPrc',
    MSG_UPDATE_METRICS = 'MMet'
};

#endif // MIDI_EVENT_HANDLER_H