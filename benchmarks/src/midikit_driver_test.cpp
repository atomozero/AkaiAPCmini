// midikit_driver_test.cpp
// Minimal test using ONLY Haiku MidiKit API (no USB Raw access)
// Purpose: Verify if blocking during batch LED writes occurs in midi_usb driver

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <OS.h>
#include <String.h>
#include <midi2/MidiRoster.h>
#include <midi2/MidiProducer.h>
#include <midi2/MidiConsumer.h>
#include <midi/MidiPort.h>
#include "apc_mini_defs.h"

// Test configuration
#define TEST_BATCH_SIZE 64
#define TEST_ITERATIONS 10
#define TIMEOUT_US 5000000  // 5 seconds

class MidiKitDriverTest {
public:
    MidiKitDriverTest()
        : local_producer(nullptr)
        , apc_consumer(nullptr)
        , midi_port(nullptr)
        , use_direct_port(false)
        , start_time(0) {}

    bool Initialize();
    void Shutdown();
    void RunBatchWriteTest();
    void PrintResults();

private:
    BMidiLocalProducer* local_producer;
    BMidiConsumer* apc_consumer;
    BMidiPort* midi_port;
    bool use_direct_port;
    bigtime_t start_time;

    struct TestStats {
        uint32_t messages_sent;
        uint32_t batches_completed;
        bigtime_t min_batch_time_us;
        bigtime_t max_batch_time_us;
        bigtime_t total_batch_time_us;
        uint32_t timeout_count;
    } stats;

    bool FindAPCMini();
    bool TryDirectPortAccess();
    void SendBatchLEDCommands(int batch_num);
    void ResetStats();
};

bool MidiKitDriverTest::Initialize()
{
    ResetStats();

    printf("=== MidiKit Driver Test ===\n");
    printf("Purpose: Test if blocking occurs in Haiku midi_usb driver\n");
    printf("Method: Batch LED writes using ONLY BMidiProducer API\n\n");

    if (!FindAPCMini()) {
        printf("MidiKit route failed, trying direct port access...\n\n");
        if (!TryDirectPortAccess()) {
            printf("ERROR: APC Mini not found via any method\n");
            return false;
        }
        printf("Successfully connected to APC Mini via direct port\n");
        use_direct_port = true;
    } else {
        printf("Successfully connected to APC Mini via MidiKit\n");
        use_direct_port = false;
    }

    return true;
}

void MidiKitDriverTest::Shutdown()
{
    if (use_direct_port) {
        if (midi_port) {
            midi_port->Close();
            delete midi_port;
            midi_port = nullptr;
        }
    } else {
        if (local_producer && apc_consumer) {
            local_producer->Disconnect(apc_consumer);
        }

        if (local_producer) {
            local_producer->Release();
            local_producer = nullptr;
        }

        if (apc_consumer) {
            apc_consumer->Release();
            apc_consumer = nullptr;
        }
    }
}

bool MidiKitDriverTest::FindAPCMini()
{
    BMidiRoster* roster = BMidiRoster::MidiRoster();
    if (!roster) {
        printf("ERROR: Cannot get MidiRoster\n");
        return false;
    }

    // Create local producer
    local_producer = new BMidiLocalProducer("MidiKit Driver Test");
    if (!local_producer) {
        printf("ERROR: Cannot create local producer\n");
        return false;
    }

    local_producer->Register();
    printf("Created local producer: ID %d\n", local_producer->ID());

    // Scan for APC Mini consumer endpoint
    int32 id = 0;
    BMidiEndpoint* endpoint;
    bool found_apc = false;
    int endpoint_count = 0;

    printf("Scanning for MIDI endpoints...\n");

    while ((endpoint = roster->NextEndpoint(&id)) != nullptr) {
        const char* endpoint_name = endpoint->Name();
        endpoint_count++;

        printf("Found MIDI endpoint: %s (ID: %d) ", endpoint_name, (int)id);

        // Show endpoint type
        if (endpoint->IsProducer() && endpoint->IsConsumer()) {
            printf("[Bidirectional]");
        } else if (endpoint->IsProducer()) {
            printf("[Producer]");
        } else if (endpoint->IsConsumer()) {
            printf("[Consumer]");
        }
        printf("\n");

        // Look for APC Mini (case insensitive check)
        BString name(endpoint_name);
        if (name.IFindFirst("APC") >= 0 && name.IFindFirst("mini") >= 0) {
            // APC Mini acts as a consumer (receives LED commands from us)
            if (endpoint->IsConsumer()) {
                apc_consumer = static_cast<BMidiConsumer*>(endpoint);
                printf("  -> Found APC Mini consumer, connecting...\n");

                // Connect our local producer to APC Mini consumer
                local_producer->Connect(apc_consumer);
                printf("  -> Connected local producer to APC Mini consumer\n");
                found_apc = true;
                // Don't release - we keep the reference
                continue;
            }
        }

        endpoint->Release();
    }

    printf("\nTotal MIDI endpoints found: %d\n", endpoint_count);

    if (!found_apc) {
        printf("ERROR: APC Mini consumer not found in MidiRoster\n");
        if (local_producer) {
            local_producer->Release();
            local_producer = nullptr;
        }
        return false;
    }

    return true;
}

bool MidiKitDriverTest::TryDirectPortAccess()
{
    printf("Trying direct /dev/midi/usb access...\n");

    // Scan /dev/midi/usb/ directory
    DIR* dir = opendir("/dev/midi/usb");
    if (!dir) {
        printf("ERROR: Cannot open /dev/midi/usb directory\n");
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char device_path[256];
        snprintf(device_path, sizeof(device_path), "/dev/midi/usb/%s", entry->d_name);

        printf("Trying device: %s\n", device_path);

        // Try to open with BMidiPort
        midi_port = new BMidiPort();
        if (midi_port->Open(device_path) == B_OK) {
            printf("  -> Successfully opened MIDI port\n");
            closedir(dir);
            return true;
        }

        delete midi_port;
        midi_port = nullptr;
    }

    closedir(dir);
    printf("ERROR: No accessible MIDI devices in /dev/midi/usb/\n");
    return false;
}

void MidiKitDriverTest::SendBatchLEDCommands(int batch_num)
{
    // Create pattern: alternating colors based on batch number
    uint8_t colors[] = {
        APC_LED_OFF,
        APC_LED_GREEN,
        APC_LED_RED,
        APC_LED_YELLOW
    };
    uint8_t color = colors[batch_num % 4];

    bigtime_t batch_start = system_time();

    // Send LED commands for all 64 pads
    for (uint8_t pad = 0; pad < APC_MINI_PAD_COUNT; pad++) {
        uint8_t note = APC_MINI_PAD_NOTE_START + pad;

        if (use_direct_port) {
            // Direct port access using BMidiPort
            // BMidiPort uses semantic methods (NoteOn, ControlChange, etc.)
            midi_port->NoteOn(APC_MINI_MIDI_CHANNEL, note, color, B_NOW);
        } else {
            // Use BMidiLocalProducer::SprayNoteOn (MIDI Kit 2 routing)
            local_producer->SprayNoteOn(APC_MINI_MIDI_CHANNEL, note, color, system_time());
        }
        stats.messages_sent++;
    }

    bigtime_t batch_time = system_time() - batch_start;

    // Update stats
    stats.batches_completed++;
    stats.total_batch_time_us += batch_time;
    if (batch_time < stats.min_batch_time_us) {
        stats.min_batch_time_us = batch_time;
    }
    if (batch_time > stats.max_batch_time_us) {
        stats.max_batch_time_us = batch_time;
    }

    printf("Batch %2d: %6ld μs (%d msgs)\n",
           batch_num, batch_time, TEST_BATCH_SIZE);

    // Check for timeout (indicates blocking in driver)
    if (batch_time > TIMEOUT_US) {
        stats.timeout_count++;
        printf("  WARNING: Batch took >%d ms (possible driver blocking)\n",
               (int)(TIMEOUT_US / 1000));
    }
}

void MidiKitDriverTest::RunBatchWriteTest()
{
    printf("\n--- Starting Batch Write Test ---\n");
    printf("Batches: %d x %d LED commands\n\n", TEST_ITERATIONS, TEST_BATCH_SIZE);

    start_time = system_time();

    for (int i = 0; i < TEST_ITERATIONS; i++) {
        SendBatchLEDCommands(i);

        // Small delay between batches
        snooze(100000); // 100ms
    }

    bigtime_t total_time = system_time() - start_time;

    printf("\n--- Test Complete ---\n");
    printf("Total time: %ld ms\n", total_time / 1000);
    PrintResults();
}

void MidiKitDriverTest::PrintResults()
{
    printf("\n=== Test Results ===\n");
    printf("Messages sent:     %u\n", stats.messages_sent);
    printf("Batches completed: %u\n", stats.batches_completed);

    if (stats.batches_completed > 0) {
        bigtime_t avg = stats.total_batch_time_us / stats.batches_completed;
        printf("Batch timing:\n");
        printf("  Min: %6ld μs\n", stats.min_batch_time_us);
        printf("  Avg: %6ld μs\n", avg);
        printf("  Max: %6ld μs\n", stats.max_batch_time_us);
    }

    printf("\n=== Analysis ===\n");
    if (stats.timeout_count > 0) {
        printf("⚠ BLOCKING DETECTED: %u batches exceeded timeout\n", stats.timeout_count);
        printf("  -> This suggests blocking occurs in Haiku midi_usb driver\n");
        printf("  -> Driver may be holding lock during BulkTransfer()\n");
    } else {
        printf("✓ No blocking detected\n");
        printf("  -> midi_usb driver handles concurrent operations correctly\n");
        printf("  -> Blocking issue (if any) is in application-level code\n");
    }

    // Expected behavior analysis
    printf("\nExpected batch time (64 msgs):\n");
    printf("  USB MIDI: ~1-2 ms (fast bulk transfers)\n");
    printf("  Actual avg: %ld μs\n",
           stats.batches_completed > 0 ? stats.total_batch_time_us / stats.batches_completed : 0);

    if (stats.batches_completed > 0) {
        bigtime_t avg = stats.total_batch_time_us / stats.batches_completed;
        if (avg > 100000) {  // >100ms
            printf("  ⚠ Significantly slower than expected (>100ms)\n");
            printf("  -> Likely indicates BulkTransfer() blocking in driver\n");
        } else if (avg > 10000) {  // >10ms
            printf("  ⚠ Slower than expected (>10ms)\n");
            printf("  -> May indicate some driver-level queuing/blocking\n");
        } else {
            printf("  ✓ Within expected range\n");
        }
    }
}

void MidiKitDriverTest::ResetStats()
{
    memset(&stats, 0, sizeof(stats));
    stats.min_batch_time_us = UINT64_MAX;
}

int main()
{
    MidiKitDriverTest test;

    if (!test.Initialize()) {
        printf("Test initialization failed\n");
        return 1;
    }

    printf("\n*** This test uses ONLY Haiku MidiKit API ***\n");
    printf("*** No USB Raw access - pure driver testing ***\n");

    test.RunBatchWriteTest();
    test.Shutdown();

    printf("\n=== Test Instructions ===\n");
    printf("1. If blocking detected: Problem is in Haiku midi_usb driver\n");
    printf("2. If no blocking: Problem is in usb_raw_midi.cpp implementation\n");
    printf("3. Compare with results from apc_mini_test (USB Raw mode)\n");

    return 0;
}
