#include "midi_message_queue.h"
#include <cstring>
#include <algorithm>

// BMessage field name constants for MIDI message serialization
const char* MIDI_MSG_STATUS = "midi:status";
const char* MIDI_MSG_DATA1 = "midi:data1";
const char* MIDI_MSG_DATA2 = "midi:data2";
const char* MIDI_MSG_SOURCE = "midi:source";
const char* MIDI_MSG_TIMESTAMP = "midi:timestamp";
const char* MIDI_MSG_SEQUENCE = "midi:sequence";

MIDIMessageQueue::MIDIMessageQueue() {
    // Initialize buffer (defensive programming)
    for (size_t i = 0; i < MIDI_QUEUE_SIZE; ++i) {
        buffer[i] = MIDIMessage{};
    }

    // Initialize statistics
    stats.last_reset_time = system_time();
    stats.overflow_events = 0;
}

MIDIMessageQueue::~MIDIMessageQueue() {
    // Nothing to explicitly clean up - all members are automatic or atomic
    // Buffer is stack-allocated and will be automatically deallocated
}

bool MIDIMessageQueue::Enqueue(const MIDIMessage& message) {
    // Get current write position (atomic read with acquire ordering)
    const uint32_t current_write = write_index.load(ACQUIRE);
    const uint32_t next_write = GetNextIndex(current_write);

    // Check if queue is full by comparing with read position
    // Using relaxed ordering for read since we only need approximate check
    const uint32_t current_read = read_index.load(RELAXED);

    if (next_write == current_read) {
        // Queue is full - drop message to maintain real-time guarantee
        // Update statistics atomically
        stats.messages_dropped.fetch_add(1, RELAXED);
        stats.overflow_events++;  // Updated by producer, read by consumer
        return false;
    }

    // We have space - prepare the message
    MIDIMessage msg_copy = message;

    // Assign sequence number for ordering validation
    msg_copy.sequence = sequence_counter.fetch_add(1, RELAXED);

    // Ensure timestamp is set
    if (msg_copy.timestamp == 0) {
        msg_copy.timestamp = system_time();
    }

    // Store message in buffer at current write position
    // This is safe because we verified space availability above
    buffer[current_write] = msg_copy;

    // Update write index with release ordering to ensure message visibility
    // This guarantees that the message data is visible before the index update
    write_index.store(next_write, RELEASE);

    // Update statistics
    stats.messages_enqueued.fetch_add(1, RELAXED);
    stats.source_counts[message.source].fetch_add(1, RELAXED);

    // Update queue depth statistics
    const uint32_t new_depth = GetQueueDepth();
    UpdateQueueDepthStats(new_depth);

    return true;
}

bool MIDIMessageQueue::EnqueueMIDI(uint8_t status, uint8_t data1, uint8_t data2,
                                  MIDIMessageSource source) {
    MIDIMessage message(status, data1, data2, source);
    return Enqueue(message);
}

bool MIDIMessageQueue::Dequeue(MIDIMessage& message) {
    // Get current read position (atomic read with acquire ordering)
    const uint32_t current_read = read_index.load(ACQUIRE);

    // Check if queue is empty by comparing with write position
    // Using acquire ordering to ensure we see all enqueued messages
    const uint32_t current_write = write_index.load(ACQUIRE);

    if (current_read == current_write) {
        // Queue is empty
        return false;
    }

    // Get message from buffer at current read position
    message = buffer[current_read];

    // Update read index with release ordering
    // This makes the slot available for new messages
    const uint32_t next_read = GetNextIndex(current_read);
    read_index.store(next_read, RELEASE);

    // Update latency statistics
    UpdateLatencyStats(message.timestamp);

    // Update statistics
    stats.messages_dequeued.fetch_add(1, RELAXED);

    return true;
}

bool MIDIMessageQueue::Peek(MIDIMessage& message) const {
    // Get current read position (atomic read with acquire ordering)
    const uint32_t current_read = read_index.load(ACQUIRE);

    // Check if queue is empty by comparing with write position
    const uint32_t current_write = write_index.load(ACQUIRE);

    if (current_read == current_write) {
        // Queue is empty
        return false;
    }

    // Copy message without updating read index
    message = buffer[current_read];
    return true;
}

uint32_t MIDIMessageQueue::GetQueueDepth() const {
    // Get current indices with relaxed ordering (approximate values)
    const uint32_t current_write = write_index.load(RELAXED);
    const uint32_t current_read = read_index.load(RELAXED);

    // Calculate depth considering ring buffer wraparound
    if (current_write >= current_read) {
        return current_write - current_read;
    } else {
        // Wraparound case
        return (MIDI_QUEUE_SIZE - current_read) + current_write;
    }
}

bool MIDIMessageQueue::IsEmpty() const {
    const uint32_t current_read = read_index.load(RELAXED);
    const uint32_t current_write = write_index.load(RELAXED);
    return current_read == current_write;
}

bool MIDIMessageQueue::IsFull() const {
    const uint32_t current_write = write_index.load(RELAXED);
    const uint32_t current_read = read_index.load(RELAXED);
    const uint32_t next_write = GetNextIndex(current_write);
    return next_write == current_read;
}

MIDIQueueStatsSnapshot MIDIMessageQueue::GetStatistics() const {
    // Return a snapshot of current statistics
    // Note: Individual values may be slightly inconsistent due to concurrent
    // updates, but this provides a good overall picture for monitoring
    MIDIQueueStatsSnapshot snapshot;

    // Atomic values
    snapshot.messages_enqueued = stats.messages_enqueued.load(RELAXED);
    snapshot.messages_dequeued = stats.messages_dequeued.load(RELAXED);
    snapshot.messages_dropped = stats.messages_dropped.load(RELAXED);
    snapshot.max_queue_depth = stats.max_queue_depth.load(RELAXED);
    snapshot.total_latency_us = stats.total_latency_us.load(RELAXED);
    snapshot.max_latency_us = stats.max_latency_us.load(RELAXED);

    for (int i = 0; i < 4; i++) {
        snapshot.source_counts[i] = stats.source_counts[i].load(RELAXED);
    }

    // Non-atomic values (consumer-only)
    snapshot.last_reset_time = stats.last_reset_time;
    snapshot.overflow_events = stats.overflow_events;

    return snapshot;
}

void MIDIMessageQueue::ResetStatistics() {
    // Reset all atomic counters
    stats.messages_enqueued.store(0, RELAXED);
    stats.messages_dequeued.store(0, RELAXED);
    stats.messages_dropped.store(0, RELAXED);
    stats.max_queue_depth.store(0, RELAXED);
    stats.total_latency_us.store(0, RELAXED);
    stats.max_latency_us.store(0, RELAXED);

    for (int i = 0; i < 4; i++) {
        stats.source_counts[i].store(0, RELAXED);
    }

    // Reset non-atomic values
    stats.last_reset_time = system_time();
    stats.overflow_events = 0;
}

BMessage* MIDIMessageQueue::CreateBMessage(const MIDIMessage& midi_msg, uint32 what) {
    BMessage* msg = new BMessage(what);

    // Add MIDI message fields
    msg->AddUInt8(MIDI_MSG_STATUS, midi_msg.status);
    msg->AddUInt8(MIDI_MSG_DATA1, midi_msg.data1);
    msg->AddUInt8(MIDI_MSG_DATA2, midi_msg.data2);
    msg->AddUInt8(MIDI_MSG_SOURCE, midi_msg.source);
    msg->AddInt64(MIDI_MSG_TIMESTAMP, midi_msg.timestamp);
    msg->AddUInt32(MIDI_MSG_SEQUENCE, midi_msg.sequence);

    return msg;
}

bool MIDIMessageQueue::ExtractFromBMessage(BMessage* bmsg, MIDIMessage& midi_msg) {
    if (!bmsg) {
        return false;
    }

    // Extract MIDI message fields with type checking
    if (bmsg->FindUInt8(MIDI_MSG_STATUS, &midi_msg.status) != B_OK ||
        bmsg->FindUInt8(MIDI_MSG_DATA1, &midi_msg.data1) != B_OK ||
        bmsg->FindUInt8(MIDI_MSG_DATA2, &midi_msg.data2) != B_OK ||
        bmsg->FindUInt8(MIDI_MSG_SOURCE, &midi_msg.source) != B_OK ||
        bmsg->FindInt64(MIDI_MSG_TIMESTAMP, &midi_msg.timestamp) != B_OK ||
        bmsg->FindUInt32(MIDI_MSG_SEQUENCE, &midi_msg.sequence) != B_OK) {
        return false;
    }

    return true;
}

void MIDIMessageQueue::UpdateLatencyStats(bigtime_t message_timestamp) {
    const bigtime_t current_time = system_time();
    const bigtime_t latency = current_time - message_timestamp;

    // Update total latency (for average calculation)
    stats.total_latency_us.fetch_add(latency, RELAXED);

    // Update maximum latency using compare-and-swap loop
    uint32_t current_max = stats.max_latency_us.load(RELAXED);
    uint32_t new_latency = static_cast<uint32_t>(std::min(latency, (bigtime_t)UINT32_MAX));

    while (new_latency > current_max) {
        if (stats.max_latency_us.compare_exchange_weak(current_max, new_latency, RELAXED)) {
            break;
        }
        // current_max was updated by compare_exchange_weak, try again
    }
}

void MIDIMessageQueue::UpdateQueueDepthStats(uint32_t current_depth) {
    // Update maximum queue depth using compare-and-swap loop
    uint64_t current_max = stats.max_queue_depth.load(RELAXED);
    uint64_t depth_64 = static_cast<uint64_t>(current_depth);

    while (depth_64 > current_max) {
        if (stats.max_queue_depth.compare_exchange_weak(current_max, depth_64, RELAXED)) {
            break;
        }
        // current_max was updated by compare_exchange_weak, try again
    }
}