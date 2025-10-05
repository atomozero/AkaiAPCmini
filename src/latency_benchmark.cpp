// Latency Benchmark Tool for APC Mini - USB Raw vs MIDI API
// Measures round-trip latency for both communication methods

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <OS.h>
#include <MidiRoster.h>
#include <MidiProducer.h>
#include <MidiConsumer.h>

#include "usb_raw_midi.h"
#include "apc_mini_defs.h"

// Benchmark configuration
#define BENCHMARK_ITERATIONS 20  // Reduced from 100 for manual testing
#define WARMUP_ITERATIONS 3      // Reduced from 10 for manual testing
#define PAD_NOTE_TEST 0x38       // Top-left pad (row 7, col 0 = 7*8+0 = 56 = 0x38)

// Statistics structure
struct BenchmarkStats {
    bigtime_t min_latency;
    bigtime_t max_latency;
    bigtime_t total_latency;
    int success_count;
    int failure_count;

    void Reset() {
        min_latency = B_INFINITE_TIMEOUT;
        max_latency = 0;
        total_latency = 0;
        success_count = 0;
        failure_count = 0;
    }

    double GetAverageLatency() const {
        return success_count > 0 ? (double)total_latency / success_count : 0.0;
    }

    void RecordMeasurement(bigtime_t latency) {
        success_count++;
        total_latency += latency;
        if (latency < min_latency) min_latency = latency;
        if (latency > max_latency) max_latency = latency;
    }

    void PrintResults(const char* method_name) const {
        printf("\n=== %s Results ===\n", method_name);
        printf("  Successful measurements: %d\n", success_count);
        printf("  Failed measurements: %d\n", failure_count);
        if (success_count > 0) {
            printf("  Minimum latency: %.2f ms\n", min_latency / 1000.0);
            printf("  Maximum latency: %.2f ms\n", max_latency / 1000.0);
            printf("  Average latency: %.2f ms\n", GetAverageLatency() / 1000.0);
        }
    }
};

// MIDI API test classes
class MIDILatencyConsumer : public BMidiLocalConsumer {
public:
    MIDILatencyConsumer() : BMidiLocalConsumer("APC Mini Latency Test Consumer"),
                            waiting_for_response(false), response_time(0) {
        SetName("Latency Test Input");
    }

    virtual void NoteOn(uchar channel, uchar note, uchar velocity, bigtime_t time) {
        (void)channel;
        (void)velocity;
        (void)time;
        if (waiting_for_response && note == PAD_NOTE_TEST) {
            response_time = system_time();
            waiting_for_response = false;
        }
    }

    void StartWaiting() {
        waiting_for_response = true;
    }

    bool IsWaiting() const {
        return waiting_for_response;
    }

    bigtime_t GetResponseTime() const {
        return response_time;
    }

private:
    volatile bool waiting_for_response;
    volatile bigtime_t response_time;
};

class MIDILatencyProducer : public BMidiLocalProducer {
public:
    MIDILatencyProducer() : BMidiLocalProducer("APC Mini Latency Test Producer") {
        SetName("Latency Test Output");
    }
};

bool RunMIDILatencyTest(BMidiProducer* apc_producer, BMidiConsumer* apc_consumer, BenchmarkStats& stats) {
    if (!apc_producer || !apc_consumer) return false;

    // Create local consumer to receive responses (must use new, not stack allocation)
    MIDILatencyConsumer* local_consumer = new MIDILatencyConsumer();
    if (local_consumer->Register() != B_OK) {
        printf("ERROR: Failed to register consumer\n");
        local_consumer->Release();
        return false;
    }

    // Create local producer to send commands (must use new, not stack allocation)
    MIDILatencyProducer* local_producer = new MIDILatencyProducer();
    if (local_producer->Register() != B_OK) {
        printf("ERROR: Failed to register producer\n");
        local_consumer->Release();
        local_producer->Release();
        return false;
    }

    // Connect: APC Producer -> Our Consumer (to receive responses)
    if (apc_producer->Connect(local_consumer) != B_OK) {
        printf("ERROR: Failed to connect APC producer to consumer\n");
        local_producer->Release();
        local_consumer->Release();
        return false;
    }

    // Connect: Our Producer -> APC Consumer (to send commands)
    if (local_producer->Connect(apc_consumer) != B_OK) {
        printf("ERROR: Failed to connect producer to APC consumer\n");
        apc_producer->Disconnect(local_consumer);
        local_producer->Release();
        local_consumer->Release();
        return false;
    }

    printf("\n🎹 Testing MIDI API latency...\n");
    printf("   Please press the TOP-LEFT pad when it lights up\n");
    printf("   Press it as quickly as possible after it lights up\n\n");

    printf("   Setting up visual grid...\n");

    // Wake up the device by sending a few dummy LED commands first
    printf("   Waking up MIDI connection...\n");
    for (int i = 0; i < 5; i++) {
        local_producer->SprayNoteOn(0, 0, 0, system_time());
        snooze(20000); // 20ms between wake-up commands
    }

    // Turn all other pads RED to highlight the yellow one
    // Add delay between each LED to avoid USB overflow
    for (uint8_t pad = 0; pad < 64; pad++) {
        if (pad != PAD_NOTE_TEST) {
            local_producer->SprayNoteOn(0, pad, 5, system_time());  // Red on MK2
            snooze(10000); // 10ms delay between each LED (increased from 5ms)
        }
    }
    printf("   Ready! Watch for the yellow LED.\n\n");
    snooze(500000); // 500ms to see the grid

    // Run the test
    for (int i = 0; i < WARMUP_ITERATIONS + BENCHMARK_ITERATIONS; i++) {
        // Light up the pad YELLOW to indicate which one to press
        // MK2 uses velocity 13 for yellow
        local_producer->SprayNoteOn(0, PAD_NOTE_TEST, 13, system_time());
        snooze(50000); // 50ms to see the LED

        // Start waiting for user response
        local_consumer->StartWaiting();
        bigtime_t send_time = system_time();

        // Wait for user to press the pad (with timeout)
        bigtime_t timeout = send_time + 2000000; // 2 second timeout for user input
        while (local_consumer->IsWaiting() && system_time() < timeout) {
            snooze(1000); // 1ms polling interval
        }

        if (!local_consumer->IsWaiting()) {
            bigtime_t latency = local_consumer->GetResponseTime() - send_time;
            // Always show feedback
            if (i >= WARMUP_ITERATIONS) {
                stats.RecordMeasurement(latency);
                printf("   ✓ Measurement %d/%d: %.2f ms\n",
                       i - WARMUP_ITERATIONS + 1, BENCHMARK_ITERATIONS, latency / 1000.0);
            } else {
                printf("   Warmup %d/%d: %.2f ms\n", i + 1, WARMUP_ITERATIONS, latency / 1000.0);
            }
            // Brief flash to confirm
            local_producer->SprayNoteOn(0, PAD_NOTE_TEST, 15, system_time());
            snooze(100000); // 100ms flash
        } else {
            if (i >= WARMUP_ITERATIONS) {
                stats.failure_count++;
                printf("   ✗ Timeout %d/%d - no response\n", i - WARMUP_ITERATIONS + 1, BENCHMARK_ITERATIONS);
            } else {
                printf("   ✗ Warmup timeout %d/%d\n", i + 1, WARMUP_ITERATIONS);
            }
        }

        // Turn off LED
        local_producer->SprayNoteOff(0, PAD_NOTE_TEST, 0, system_time());
        snooze(500000); // 500ms delay between tests for user to react
    }

    // Turn off all LEDs at the end
    printf("\n   Turning off LEDs...\n");
    for (uint8_t pad = 0; pad < 64; pad++) {
        local_producer->SprayNoteOff(0, pad, 0, system_time());
        snooze(5000); // 5ms delay between each LED
    }

    // Cleanup - disconnect and release endpoints
    apc_producer->Disconnect(local_consumer);
    local_producer->Disconnect(apc_consumer);

    // Use Release() instead of delete or Unregister() - Haiku requirement
    local_producer->Release();
    local_consumer->Release();

    return stats.success_count > 0;
}

// USB Raw latency test with callback
static volatile bool usb_waiting_for_response = false;
static volatile bigtime_t usb_response_time = 0;

void USBLatencyCallback(uint8_t status, uint8_t data1, uint8_t data2) {
    (void)data2;  // Unused parameter
    if (usb_waiting_for_response && status == 0x90 && data1 == PAD_NOTE_TEST) {
        usb_response_time = system_time();
        usb_waiting_for_response = false;
    }
}

bool RunUSBRawLatencyTest(USBRawMIDI* usb, BenchmarkStats& stats) {
    if (!usb || !usb->IsConnected()) {
        printf("ERROR: USB Raw device not available\n");
        return false;
    }

    // Set callback for receiving MIDI
    usb->SetMIDICallback(USBLatencyCallback);

    printf("\n🎹 Testing USB Raw latency...\n");
    printf("   Please press the TOP-LEFT pad when it lights up\n");
    printf("   Press it as quickly as possible after it lights up\n\n");

    printf("   Setting up visual grid...\n");

    // Pause reader thread to avoid USB conflicts
    usb->PauseReader();

    // Send Introduction Message (required by APC Mini MK2 protocol)
    // Must be sent while reader is paused to avoid deadlock
    usb->SendIntroductionMessage();

    // Turn all other pads RED to highlight the green one
    for (uint8_t pad = 0; pad < 64; pad++) {
        if (pad != PAD_NOTE_TEST) {
            APCMiniError result = usb->SetPadColor(pad, static_cast<APCMiniLEDColor>(5));  // Red on MK2
            if (result != APC_SUCCESS) {
                printf("   Warning: Failed to set LED %d\n", pad);
            }
        }
    }

    // Resume reader thread
    usb->ResumeReader();

    printf("   Ready! Watch for the green LED.\n\n");
    snooze(500000); // 500ms to see the grid

    for (int i = 0; i < WARMUP_ITERATIONS + BENCHMARK_ITERATIONS; i++) {
        // Light up the pad GREEN to indicate which one to press
        usb->SetPadColor(PAD_NOTE_TEST, static_cast<APCMiniLEDColor>(21));  // Green on MK2
        snooze(50000); // 50ms to see the LED

        // Send note-on and start timing
        usb_waiting_for_response = true;
        bigtime_t send_time = system_time();

        // Wait for user to press the pad (with timeout)
        bigtime_t timeout = send_time + 2000000; // 2 second timeout for user input
        while (usb_waiting_for_response && system_time() < timeout) {
            snooze(1000); // 1ms polling interval
        }

        if (!usb_waiting_for_response) {
            bigtime_t latency = usb_response_time - send_time;
            // Always show feedback
            if (i >= WARMUP_ITERATIONS) {
                stats.RecordMeasurement(latency);
                printf("   ✓ Measurement %d/%d: %.2f ms\n",
                       i - WARMUP_ITERATIONS + 1, BENCHMARK_ITERATIONS, latency / 1000.0);
            } else {
                printf("   Warmup %d/%d: %.2f ms\n", i + 1, WARMUP_ITERATIONS, latency / 1000.0);
            }
            // Brief flash to confirm
            usb->SetPadColor(PAD_NOTE_TEST, static_cast<APCMiniLEDColor>(25));
            snooze(100000); // 100ms flash
        } else {
            if (i >= WARMUP_ITERATIONS) {
                stats.failure_count++;
                printf("   ✗ Timeout %d/%d - no response\n", i - WARMUP_ITERATIONS + 1, BENCHMARK_ITERATIONS);
            } else {
                printf("   ✗ Warmup timeout %d/%d\n", i + 1, WARMUP_ITERATIONS);
            }
        }

        // Turn off LED
        usb->SetPadColor(PAD_NOTE_TEST, APC_LED_OFF);
        snooze(500000); // 500ms delay between tests for user to react
    }

    // Turn off all LEDs at the end
    printf("\n   Turning off LEDs...\n");
    usb->PauseReader();
    for (uint8_t pad = 0; pad < 64; pad++) {
        usb->SetPadColor(pad, APC_LED_OFF);
    }
    usb->ResumeReader();

    return stats.success_count > 0;
}

void PrintBenchmarkHeader() {
    printf("\n");
    printf("========================================\n");
    printf("  APC Mini Latency Benchmark Tool\n");
    printf("========================================\n");
    printf("Testing round-trip latency:\n");
    printf("  - USB Raw access vs MIDI API\n");
    printf("  - %d iterations (after %d warmup)\n", BENCHMARK_ITERATIONS, WARMUP_ITERATIONS);
    printf("  - Measures Note On -> Echo response\n");
    printf("========================================\n\n");
}

void PrintComparison(const BenchmarkStats& usb_stats, const BenchmarkStats& midi_stats) {
    printf("\n");
    printf("========================================\n");
    printf("  Comparison Summary\n");
    printf("========================================\n");

    if (usb_stats.success_count > 0 && midi_stats.success_count > 0) {
        double usb_avg = usb_stats.GetAverageLatency();
        double midi_avg = midi_stats.GetAverageLatency();
        double improvement = ((midi_avg - usb_avg) / midi_avg) * 100.0;

        printf("Average latency:\n");
        printf("  USB Raw: %.2f ms\n", usb_avg / 1000.0);
        printf("  MIDI API: %.2f ms\n", midi_avg / 1000.0);
        printf("\n");

        if (improvement > 0) {
            printf("USB Raw is %.1f%% faster than MIDI API\n", improvement);
        } else {
            printf("MIDI API is %.1f%% faster than USB Raw\n", -improvement);
        }

        printf("\nLatency ranges:\n");
        printf("  USB Raw: %.2f - %.2f ms\n",
               usb_stats.min_latency / 1000.0, usb_stats.max_latency / 1000.0);
        printf("  MIDI API: %.2f - %.2f ms\n",
               midi_stats.min_latency / 1000.0, midi_stats.max_latency / 1000.0);
    }

    printf("========================================\n\n");
}

int main(int argc, char** argv) {
    PrintBenchmarkHeader();

    bool test_usb = true;
    bool test_midi = true;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--usb-only") == 0) {
            test_midi = false;
        } else if (strcmp(argv[i], "--midi-only") == 0) {
            test_usb = false;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --usb-only    Test only USB Raw access\n");
            printf("  --midi-only   Test only MIDI API\n");
            printf("  --help        Show this help\n");
            return 0;
        }
    }

    BenchmarkStats usb_stats, midi_stats;
    usb_stats.Reset();
    midi_stats.Reset();

    // Test USB Raw
    if (test_usb) {
        printf("Testing USB Raw access...\n");
        USBRawMIDI usb;

        if (usb.Initialize() == APC_SUCCESS) {
            if (!RunUSBRawLatencyTest(&usb, usb_stats)) {
                printf("WARNING: USB Raw test failed\n");
            }
            usb.Shutdown();
        } else {
            printf("ERROR: Failed to initialize USB Raw device\n");
            test_usb = false;
        }
    }

    // Test MIDI API
    if (test_midi) {
        printf("\nTesting MIDI API...\n");

        BMidiRoster* roster = BMidiRoster::MidiRoster();
        if (!roster) {
            printf("ERROR: Cannot access MIDI roster\n");
            test_midi = false;
        } else {
            // Find APC Mini producer and consumer
            BMidiProducer* apc_producer = nullptr;
            BMidiConsumer* apc_consumer = nullptr;
            int32 id = 0;

            // Find producer (search for APC Mini GUI, native APC Mini, or USB MIDI)
            while (true) {
                BMidiProducer* prod = roster->NextProducer(&id);
                if (!prod) break;

                const char* name = prod->Name();
                printf("   Found MIDI Producer: %s\n", name);
                // Match: APC Mini, APC mini mk2, /dev/midi/usb, or APC (GUI)
                if (strstr(name, "APC") || strstr(name, "/dev/midi/usb")) {
                    apc_producer = prod;
                    printf("   → Selected: %s\n", name);
                    break;
                }
                prod->Release();
            }

            // Find consumer
            id = 0;
            while (true) {
                BMidiConsumer* cons = roster->NextConsumer(&id);
                if (!cons) break;

                const char* name = cons->Name();
                printf("   Found MIDI Consumer: %s\n", name);
                // Match: APC Mini, APC mini mk2, /dev/midi/usb, or APC (GUI)
                if (strstr(name, "APC") || strstr(name, "/dev/midi/usb")) {
                    apc_consumer = cons;
                    printf("   → Selected: %s\n", name);
                    break;
                }
                cons->Release();
            }

            if (!apc_producer || !apc_consumer) {
                printf("ERROR: APC Mini MIDI endpoints not found\n");
                if (apc_producer) apc_producer->Release();
                if (apc_consumer) apc_consumer->Release();
                test_midi = false;
            } else {
                if (!RunMIDILatencyTest(apc_producer, apc_consumer, midi_stats)) {
                    printf("WARNING: MIDI API test failed\n");
                }

                apc_producer->Release();
                apc_consumer->Release();
            }
        }
    }

    // Print results
    if (test_usb) {
        usb_stats.PrintResults("USB Raw Access");
    }

    if (test_midi) {
        midi_stats.PrintResults("MIDI API");
    }

    if (test_usb && test_midi) {
        PrintComparison(usb_stats, midi_stats);
    }

    return 0;
}
