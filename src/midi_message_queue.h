#ifndef MIDI_MESSAGE_QUEUE_H
#define MIDI_MESSAGE_QUEUE_H

#include <OS.h>
#include <Message.h>
#include <atomic>
#include <stdint.h>
#include "apc_mini_defs.h"

/**
 * MIDIMessageQueue - Lock-free ring buffer for real-time MIDI message handling
 *
 * This class implements a lock-free, wait-free ring buffer specifically designed
 * for real-time MIDI applications following audio/MIDI best practices:
 *
 * REAL-TIME CONSTRAINTS:
 * - No memory allocations in hot path (pre-allocated buffer)
 * - No blocking operations (lock-free using atomic operations)
 * - No priority inversion (wait-free for producers)
 * - Bounded worst-case execution time
 * - Cache-friendly memory layout
 *
 * THREAD SAFETY MODEL:
 * - Multiple producers (USB thread, MIDI thread, GUI thread)
 * - Single consumer (GUI thread for processing and display updates)
 * - Uses memory ordering guarantees for cross-thread visibility
 *
 * DESIGN DECISIONS:
 * - Power-of-2 buffer size for efficient modulo operations using bitwise AND
 * - Separate read/write indices to avoid false sharing
 * - Message source tracking to prevent feedback loops
 * - Overflow handling that drops oldest messages (never blocks)
 * - Built-in statistics for performance monitoring
 */

// Message sources for tracking origin and preventing feedback loops
enum MIDIMessageSource {
    MIDI_SOURCE_HARDWARE_USB = 0,    // From USB Raw device
    MIDI_SOURCE_HARDWARE_MIDI = 1,   // From Haiku MIDI API
    MIDI_SOURCE_GUI = 2,             // From GUI interactions
    MIDI_SOURCE_SIMULATION = 3       // From test simulation
};

// MIDI message structure optimized for cache efficiency
struct MIDIMessage {
    uint8_t status;        // MIDI status byte (includes channel)
    uint8_t data1;         // First data byte (note/controller number)
    uint8_t data2;         // Second data byte (velocity/value)
    uint8_t source;        // MIDIMessageSource enum value
    uint8_t priority;      // Event priority for real-time scheduling
    uint16_t sysex_length; // Length of SysEx data (0 for non-SysEx)
    bigtime_t timestamp;   // Haiku high-resolution timestamp
    uint32_t sequence;     // Sequence number for ordering validation

    MIDIMessage() : status(0), data1(0), data2(0), source(MIDI_SOURCE_HARDWARE_USB),
                   priority(2), sysex_length(0), timestamp(0), sequence(0) {}

    MIDIMessage(uint8_t s, uint8_t d1, uint8_t d2, MIDIMessageSource src, bigtime_t ts = 0)
        : status(s), data1(d1), data2(d2), source(static_cast<uint8_t>(src)),
          priority(2), sysex_length(0), timestamp(ts == 0 ? system_time() : ts), sequence(0) {}
};

// Queue statistics for performance monitoring
struct MIDIQueueStats {
    std::atomic<uint64_t> messages_enqueued{0};    // Total messages added
    std::atomic<uint64_t> messages_dequeued{0};    // Total messages processed
    std::atomic<uint64_t> messages_dropped{0};     // Messages lost due to overflow
    std::atomic<uint64_t> max_queue_depth{0};      // Peak queue usage
    std::atomic<uint64_t> total_latency_us{0};     // Cumulative latency
    std::atomic<uint32_t> max_latency_us{0};       // Peak latency
    std::atomic<uint32_t> source_counts[4]{0};     // Per-source message counts

    // Non-atomic stats (updated by consumer only)
    uint64_t last_reset_time;                      // When stats were last reset
    uint32_t overflow_events;                      // Number of overflow occurrences
};

// Non-atomic snapshot for returning statistics
struct MIDIQueueStatsSnapshot {
    uint64_t messages_enqueued;
    uint64_t messages_dequeued;
    uint64_t messages_dropped;
    uint64_t max_queue_depth;
    uint64_t total_latency_us;
    uint32_t max_latency_us;
    uint32_t source_counts[4];
    uint64_t last_reset_time;
    uint32_t overflow_events;
};

/**
 * Lock-free ring buffer for MIDI messages
 *
 * Buffer size is power-of-2 for efficient modulo operations.
 * Default size provides good balance of memory usage vs latency tolerance.
 * Adjust MIDI_QUEUE_SIZE_BITS to change capacity (2^bits elements).
 */
class MIDIMessageQueue {
public:
    // Buffer configuration - must be power of 2 for efficient indexing
    static constexpr uint32_t MIDI_QUEUE_SIZE_BITS = 12;  // 4096 elements
    static constexpr uint32_t MIDI_QUEUE_SIZE = 1 << MIDI_QUEUE_SIZE_BITS;
    static constexpr uint32_t MIDI_QUEUE_MASK = MIDI_QUEUE_SIZE - 1;

    // Memory ordering constants for atomic operations
    static constexpr std::memory_order ACQUIRE = std::memory_order_acquire;
    static constexpr std::memory_order RELEASE = std::memory_order_release;
    static constexpr std::memory_order RELAXED = std::memory_order_relaxed;

    MIDIMessageQueue();
    ~MIDIMessageQueue();

    /**
     * Enqueue a MIDI message (real-time safe)
     *
     * This method is designed to be called from real-time threads:
     * - Never blocks or allocates memory
     * - Uses atomic operations for thread safety
     * - Drops oldest messages on overflow (maintains real-time guarantee)
     *
     * @param message MIDI message to enqueue
     * @return true if message was enqueued, false if queue is full
     */
    bool Enqueue(const MIDIMessage& message);

    /**
     * Convenience method for common MIDI message types
     */
    bool EnqueueMIDI(uint8_t status, uint8_t data1, uint8_t data2,
                    MIDIMessageSource source);

    /**
     * Dequeue a MIDI message (consumer only)
     *
     * Should only be called from the consumer thread (typically GUI thread).
     * Updates latency statistics automatically.
     *
     * @param message Reference to store the dequeued message
     * @return true if message was dequeued, false if queue is empty
     */
    bool Dequeue(MIDIMessage& message);

    /**
     * Peek at next message without removing it
     *
     * @param message Reference to store the peeked message
     * @return true if message is available, false if queue is empty
     */
    bool Peek(MIDIMessage& message) const;

    /**
     * Get current queue depth (approximate)
     *
     * Note: This is a snapshot and may be slightly inaccurate due to
     * concurrent operations, but provides a good estimate for monitoring.
     */
    uint32_t GetQueueDepth() const;

    /**
     * Check if queue is empty (approximate)
     */
    bool IsEmpty() const;

    /**
     * Check if queue is full (approximate)
     */
    bool IsFull() const;

    /**
     * Get queue capacity
     */
    uint32_t GetCapacity() const { return MIDI_QUEUE_SIZE; }

    /**
     * Get performance statistics
     *
     * Returns current statistics. Some values may be slightly inconsistent
     * due to concurrent updates, but overall trends are accurate.
     */
    MIDIQueueStatsSnapshot GetStatistics() const;

    /**
     * Reset statistics counters
     *
     * Should only be called from consumer thread to avoid race conditions.
     */
    void ResetStatistics();

    /**
     * Create a BMessage for GUI updates
     *
     * Converts MIDI message to Haiku BMessage format for thread-safe
     * communication with GUI components.
     *
     * @param midi_msg MIDI message to convert
     * @param what Message code for BMessage
     * @return Pointer to allocated BMessage (caller owns)
     */
    static BMessage* CreateBMessage(const MIDIMessage& midi_msg, uint32 what);

    /**
     * Extract MIDI message from BMessage
     *
     * Reverse operation of CreateBMessage for receiving GUI messages.
     *
     * @param bmsg BMessage to extract from
     * @param midi_msg Reference to store extracted MIDI message
     * @return true if extraction successful
     */
    static bool ExtractFromBMessage(BMessage* bmsg, MIDIMessage& midi_msg);

private:
    // Ring buffer storage (cache-aligned for performance)
    alignas(64) MIDIMessage buffer[MIDI_QUEUE_SIZE];

    // Atomic indices for lock-free operation
    // Separated to different cache lines to avoid false sharing
    alignas(64) std::atomic<uint32_t> write_index{0};
    alignas(64) std::atomic<uint32_t> read_index{0};

    // Sequence counter for message ordering validation
    std::atomic<uint32_t> sequence_counter{0};

    // Performance statistics
    alignas(64) MIDIQueueStats stats;

    // Helper methods
    uint32_t GetNextIndex(uint32_t current) const {
        return (current + 1) & MIDI_QUEUE_MASK;
    }

    void UpdateLatencyStats(bigtime_t message_timestamp);
    void UpdateQueueDepthStats(uint32_t current_depth);

    // Prevent copying (not thread-safe and usually not desired)
    MIDIMessageQueue(const MIDIMessageQueue&) = delete;
    MIDIMessageQueue& operator=(const MIDIMessageQueue&) = delete;
};

// Constants for BMessage field names (used with CreateBMessage/ExtractFromBMessage)
extern const char* MIDI_MSG_STATUS;
extern const char* MIDI_MSG_DATA1;
extern const char* MIDI_MSG_DATA2;
extern const char* MIDI_MSG_SOURCE;
extern const char* MIDI_MSG_TIMESTAMP;
extern const char* MIDI_MSG_SEQUENCE;

#endif // MIDI_MESSAGE_QUEUE_H