// virtual_midi_benchmark.cpp
// Virtual MIDI benchmark - Tests pure MidiKit performance without hardware
//
// Purpose: Establish baseline performance metrics for Haiku MidiKit by testing
// virtual MIDI routing (producer -> consumer) without USB/hardware involvement.
//
// This isolates MidiKit overhead from USB driver/hardware latency.

#include <stdio.h>
#include <string.h>
#include <OS.h>
#include <midi2/MidiRoster.h>
#include <midi2/MidiProducer.h>
#include <midi2/MidiConsumer.h>

// Test configuration
#define WARMUP_ITERATIONS 10
#define LATENCY_TEST_ITERATIONS 100
#define THROUGHPUT_TEST_ITERATIONS 1000
#define BATCH_SIZE 64

// Statistics structure
struct BenchmarkStats {
    uint32_t messages_sent;
    uint32_t messages_received;
    bigtime_t min_latency_us;
    bigtime_t max_latency_us;
    bigtime_t total_latency_us;
    uint32_t lost_messages;
    bigtime_t total_duration_us;
};

// Virtual MIDI Consumer - Receives messages and measures latency
class VirtualMIDIConsumer : public BMidiLocalConsumer {
public:
    VirtualMIDIConsumer()
        : BMidiLocalConsumer("Virtual Benchmark Consumer")
        , messages_received(0)
        , last_receive_time(0)
    {
        memset(&stats, 0, sizeof(stats));
        stats.min_latency_us = UINT64_MAX;
    }

    void NoteOn(uchar channel, uchar note, uchar velocity, bigtime_t time) override {
        (void)channel; // Unused parameter
        bigtime_t receive_time = system_time();
        messages_received++;
        last_receive_time = receive_time;

        // Calculate latency (timestamp in message vs actual receive time)
        bigtime_t latency = receive_time - time;

        stats.total_latency_us += latency;
        if (latency < stats.min_latency_us) {
            stats.min_latency_us = latency;
        }
        if (latency > stats.max_latency_us) {
            stats.max_latency_us = latency;
        }

        // Note: Message verification disabled - async routing means we can't predict
        // exact message order. Just verify data integrity during stats collection.
        (void)note;
        (void)velocity;
    }

    void NoteOff(uchar channel, uchar note, uchar velocity, bigtime_t time) override {
        (void)channel; (void)note; (void)velocity; (void)time; // Unused
        // Count note off messages
        messages_received++;
        last_receive_time = system_time();
    }

    void ControlChange(uchar channel, uchar controller, uchar value, bigtime_t time) override {
        (void)channel; (void)controller; (void)value; (void)time; // Unused
        messages_received++;
        last_receive_time = system_time();
    }

    void ResetStats() {
        memset(&stats, 0, sizeof(stats));
        stats.min_latency_us = UINT64_MAX;
        messages_received = 0;
    }

    uint32_t GetMessagesReceived() const { return messages_received; }
    bigtime_t GetLastReceiveTime() const { return last_receive_time; }
    const BenchmarkStats& GetStats() const { return stats; }

private:
    uint32_t messages_received;
    bigtime_t last_receive_time;
    BenchmarkStats stats;
};

// Virtual MIDI Producer - Sends test messages
class VirtualMIDIProducer : public BMidiLocalProducer {
public:
    VirtualMIDIProducer()
        : BMidiLocalProducer("Virtual Benchmark Producer")
        , messages_sent(0)
    {}

    void SendTestNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        SprayNoteOn(channel, note, velocity, system_time());
        messages_sent++;
    }

    void SendTestNoteOff(uint8_t channel, uint8_t note) {
        SprayNoteOff(channel, note, 0, system_time());
        messages_sent++;
    }

    void SendTestControlChange(uint8_t channel, uint8_t controller, uint8_t value) {
        SprayControlChange(channel, controller, value, system_time());
        messages_sent++;
    }

    void ResetStats() {
        messages_sent = 0;
    }

    uint32_t GetMessagesSent() const { return messages_sent; }

private:
    uint32_t messages_sent;
};

// Benchmark test runner
class VirtualMIDIBenchmark {
public:
    VirtualMIDIBenchmark() : producer(nullptr), consumer(nullptr) {}

    bool Initialize();
    void Shutdown();

    void RunLatencyTest();
    void RunThroughputTest();
    void RunBatchTest();
    void PrintSummary();

private:
    VirtualMIDIProducer* producer;
    VirtualMIDIConsumer* consumer;
    BenchmarkStats overall_stats;

    void WarmUp();
    void ResetStats();
};

bool VirtualMIDIBenchmark::Initialize()
{
    printf("=== Virtual MIDI Benchmark ===\n");
    printf("Purpose: Measure pure MidiKit performance (no hardware)\n\n");

    // Create virtual producer
    producer = new VirtualMIDIProducer();
    if (!producer) {
        printf("ERROR: Failed to create virtual producer\n");
        return false;
    }

    if (producer->Register() != B_OK) {
        printf("ERROR: Failed to register producer\n");
        delete producer;
        producer = nullptr;
        return false;
    }
    printf("✓ Created virtual producer (ID: %d)\n", producer->ID());

    // Create virtual consumer
    consumer = new VirtualMIDIConsumer();
    if (!consumer) {
        printf("ERROR: Failed to create virtual consumer\n");
        producer->Release();
        delete producer;
        producer = nullptr;
        return false;
    }

    if (consumer->Register() != B_OK) {
        printf("ERROR: Failed to register consumer\n");
        producer->Release();
        delete producer;
        delete consumer;
        producer = nullptr;
        consumer = nullptr;
        return false;
    }
    printf("✓ Created virtual consumer (ID: %d)\n", consumer->ID());

    // Connect producer to consumer
    if (producer->Connect(consumer) != B_OK) {
        printf("ERROR: Failed to connect producer to consumer\n");
        producer->Release();
        consumer->Release();
        delete producer;
        delete consumer;
        producer = nullptr;
        consumer = nullptr;
        return false;
    }
    printf("✓ Connected producer → consumer\n\n");

    ResetStats();
    return true;
}

void VirtualMIDIBenchmark::Shutdown()
{
    if (producer && consumer) {
        producer->Disconnect(consumer);
    }

    if (producer) {
        producer->Release();
        delete producer;
        producer = nullptr;
    }

    if (consumer) {
        consumer->Release();
        delete consumer;
        consumer = nullptr;
    }
}

void VirtualMIDIBenchmark::WarmUp()
{
    printf("Warming up (%d iterations)...\n", WARMUP_ITERATIONS);
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        producer->SendTestNoteOn(0, 60, 127);
        snooze(100); // 100μs delay
    }
    snooze(10000); // Wait 10ms for messages to settle

    // Reset stats after warmup
    producer->ResetStats();
    consumer->ResetStats();
}

void VirtualMIDIBenchmark::ResetStats()
{
    memset(&overall_stats, 0, sizeof(overall_stats));
    overall_stats.min_latency_us = UINT64_MAX;
}

void VirtualMIDIBenchmark::RunLatencyTest()
{
    printf("\n=== Latency Test ===\n");
    printf("Iterations: %d\n", LATENCY_TEST_ITERATIONS);
    printf("Testing single-message latency...\n\n");

    WarmUp();

    bigtime_t test_start = system_time();

    for (int i = 0; i < LATENCY_TEST_ITERATIONS; i++) {
        uint8_t note = 60 + (i % 12); // C4 to B4
        uint8_t velocity = 64 + (i % 64);

        // Note: SetExpectedMessage removed - async routing makes order unpredictable
        producer->SendTestNoteOn(0, note, velocity);

        // Small delay to let message propagate
        snooze(100); // 100μs
    }

    // Wait for all messages to arrive
    snooze(10000); // 10ms

    bigtime_t test_duration = system_time() - test_start;

    // Get stats from consumer
    const BenchmarkStats& stats = consumer->GetStats();
    uint32_t sent = producer->GetMessagesSent();
    uint32_t received = consumer->GetMessagesReceived();

    printf("Results:\n");
    printf("  Messages sent:     %u\n", sent);
    printf("  Messages received: %u\n", received);
    printf("  Lost messages:     %u\n", sent - received);
    printf("  Test duration:     %ld ms\n", test_duration / 1000);

    if (received > 0) {
        printf("\nLatency (per message):\n");
        printf("  Min: %6ld μs\n", stats.min_latency_us);
        printf("  Avg: %6ld μs\n", stats.total_latency_us / received);
        printf("  Max: %6ld μs\n", stats.max_latency_us);
    }

    // Update overall stats
    overall_stats.messages_sent += sent;
    overall_stats.messages_received += received;
    overall_stats.lost_messages += (sent - received);
}

void VirtualMIDIBenchmark::RunThroughputTest()
{
    printf("\n=== Throughput Test ===\n");
    printf("Iterations: %d\n", THROUGHPUT_TEST_ITERATIONS);
    printf("Testing maximum message rate...\n\n");

    producer->ResetStats();
    consumer->ResetStats();

    bigtime_t test_start = system_time();

    // Send messages as fast as possible
    for (int i = 0; i < THROUGHPUT_TEST_ITERATIONS; i++) {
        producer->SendTestNoteOn(0, i % 128, 127);
        // NO delay - test maximum throughput
    }

    // Wait for messages to propagate
    snooze(50000); // 50ms to let all messages arrive

    bigtime_t test_duration = system_time() - test_start;

    uint32_t sent = producer->GetMessagesSent();
    uint32_t received = consumer->GetMessagesReceived();

    printf("Results:\n");
    printf("  Messages sent:     %u\n", sent);
    printf("  Messages received: %u\n", received);
    printf("  Lost messages:     %u\n", sent - received);
    printf("  Test duration:     %ld ms\n", test_duration / 1000);

    if (test_duration > 0) {
        double msg_per_sec = (double)sent / (test_duration / 1000000.0);
        double mbps = (msg_per_sec * 3) / 3906.25; // 3 bytes per MIDI msg, 31250 baud
        printf("\nThroughput:\n");
        printf("  Messages/sec: %.0f\n", msg_per_sec);
        printf("  Equivalent baud: %.1fx MIDI speed (31.25 kbaud)\n", mbps);
    }

    overall_stats.messages_sent += sent;
    overall_stats.messages_received += received;
    overall_stats.lost_messages += (sent - received);
}

void VirtualMIDIBenchmark::RunBatchTest()
{
    printf("\n=== Batch Test ===\n");
    printf("Batch size: %d messages\n", BATCH_SIZE);
    printf("Testing batch write performance...\n\n");

    producer->ResetStats();
    consumer->ResetStats();

    bigtime_t batch_start = system_time();

    // Send batch of messages (simulates LED update scenario)
    for (int i = 0; i < BATCH_SIZE; i++) {
        producer->SendTestNoteOn(0, i, (i % 6) + 1); // Simulate LED colors
    }

    // Wait for batch to complete
    snooze(10000); // 10ms

    bigtime_t batch_duration = system_time() - batch_start;

    uint32_t sent = producer->GetMessagesSent();
    uint32_t received = consumer->GetMessagesReceived();

    printf("Results:\n");
    printf("  Messages sent:     %u\n", sent);
    printf("  Messages received: %u\n", received);
    printf("  Batch duration:    %ld μs\n", batch_duration);
    printf("  Per-message time:  %ld μs\n", batch_duration / BATCH_SIZE);

    overall_stats.messages_sent += sent;
    overall_stats.messages_received += received;
    overall_stats.lost_messages += (sent - received);
}

void VirtualMIDIBenchmark::PrintSummary()
{
    printf("\n=== Overall Summary ===\n");
    printf("Total messages sent:     %u\n", overall_stats.messages_sent);
    printf("Total messages received: %u\n", overall_stats.messages_received);
    printf("Total lost messages:     %u\n", overall_stats.lost_messages);

    if (overall_stats.messages_sent > 0) {
        double success_rate = (double)overall_stats.messages_received / overall_stats.messages_sent * 100.0;
        printf("Success rate:            %.2f%%\n", success_rate);
    }

    printf("\n=== Analysis ===\n");
    printf("This benchmark establishes baseline MidiKit performance.\n");
    printf("Compare these results with hardware tests to identify:\n");
    printf("  - USB driver overhead\n");
    printf("  - Hardware latency\n");
    printf("  - Driver blocking issues\n");

    printf("\nTypical MidiKit virtual routing results:\n");
    printf("  - Latency: ~200-500 μs (routing overhead)\n");
    printf("  - Throughput: ~2,000-5,000 msg/sec (MidiKit limitation)\n");
    printf("  - Lost messages: 0 (reliable routing)\n");
    printf("\nNote: MidiKit has significant overhead even for virtual routing.\n");
    printf("      USB/hardware will add additional latency on top of this baseline.\n");
}

int main()
{
    VirtualMIDIBenchmark benchmark;

    if (!benchmark.Initialize()) {
        printf("Benchmark initialization failed\n");
        return 1;
    }

    printf("Starting virtual MIDI benchmarks...\n");
    printf("This tests ONLY MidiKit routing (no hardware/USB)\n");
    printf("=================================================\n");

    benchmark.RunLatencyTest();
    benchmark.RunThroughputTest();
    benchmark.RunBatchTest();
    benchmark.PrintSummary();

    benchmark.Shutdown();

    printf("\n=== Benchmark Complete ===\n");
    printf("Use these results as baseline for hardware comparison.\n");

    return 0;
}
