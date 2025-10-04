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
#define BENCHMARK_ITERATIONS 100
#define WARMUP_ITERATIONS 10
#define PAD_NOTE_TEST 0x00  // Top-left pad

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

// MIDI API test class
class MIDILatencyTest : public BMidiLocalConsumer {
public:
    MIDILatencyTest() : BMidiLocalConsumer("APC Mini Latency Test"),
                        waiting_for_response(false), response_time(0) {}

    virtual void NoteOn(uchar channel, uchar note, uchar velocity, bigtime_t time) {
        (void)channel;
        (void)velocity;
        if (waiting_for_response && note == PAD_NOTE_TEST) {
            response_time = system_time();
            waiting_for_response = false;
        }
    }

    bool RunLatencyTest(BMidiProducer* producer, BenchmarkStats& stats) {
        if (!producer) return false;

        for (int i = 0; i < WARMUP_ITERATIONS + BENCHMARK_ITERATIONS; i++) {
            // Send note-on
            waiting_for_response = true;
            bigtime_t send_time = system_time();
            producer->SprayNoteOn(0, PAD_NOTE_TEST, 127, send_time);

            // Wait for response (with timeout)
            bigtime_t timeout = send_time + 100000; // 100ms timeout
            while (waiting_for_response && system_time() < timeout) {
                snooze(100); // 0.1ms polling interval
            }

            if (!waiting_for_response) {
                bigtime_t latency = response_time - send_time;
                // Only record after warmup
                if (i >= WARMUP_ITERATIONS) {
                    stats.RecordMeasurement(latency);
                }
            } else {
                if (i >= WARMUP_ITERATIONS) {
                    stats.failure_count++;
                }
            }

            // Send note-off to cleanup
            producer->SprayNoteOff(0, PAD_NOTE_TEST, 0, system_time());
            snooze(10000); // 10ms delay between tests
        }

        return stats.success_count > 0;
    }

private:
    volatile bool waiting_for_response;
    volatile bigtime_t response_time;
};

// USB Raw latency test
bool RunUSBRawLatencyTest(USBRawMIDI* usb, BenchmarkStats& stats) {
    if (!usb || !usb->IsOpen()) {
        printf("ERROR: USB Raw device not available\n");
        return false;
    }

    uint8_t midi_msg[3];

    for (int i = 0; i < WARMUP_ITERATIONS + BENCHMARK_ITERATIONS; i++) {
        // Send note-on
        bigtime_t send_time = system_time();
        midi_msg[0] = 0x90; // Note On
        midi_msg[1] = PAD_NOTE_TEST;
        midi_msg[2] = 127;

        if (usb->SendMIDIMessage(midi_msg, 3) != APC_SUCCESS) {
            if (i >= WARMUP_ITERATIONS) {
                stats.failure_count++;
            }
            continue;
        }

        // Wait for response
        bigtime_t timeout = send_time + 100000; // 100ms timeout
        bool got_response = false;

        while (system_time() < timeout) {
            uint8_t recv_msg[3];
            int bytes = usb->ReceiveMIDIMessage(recv_msg, 3, 100); // 0.1ms timeout

            if (bytes >= 3 && recv_msg[0] == 0x90 && recv_msg[1] == PAD_NOTE_TEST) {
                bigtime_t recv_time = system_time();
                bigtime_t latency = recv_time - send_time;

                // Only record after warmup
                if (i >= WARMUP_ITERATIONS) {
                    stats.RecordMeasurement(latency);
                }
                got_response = true;
                break;
            }
        }

        if (!got_response && i >= WARMUP_ITERATIONS) {
            stats.failure_count++;
        }

        // Send note-off to cleanup
        midi_msg[0] = 0x80; // Note Off
        midi_msg[2] = 0;
        usb->SendMIDIMessage(midi_msg, 3);

        snooze(10000); // 10ms delay between tests
    }

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
            usb.Cleanup();
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
            // Find APC Mini producer
            BMidiProducer* producer = nullptr;
            int32 id = 0;

            while ((id = roster->NextProducer(&id)) > 0) {
                BMidiProducer* prod = roster->FindProducer(id);
                if (prod && strstr(prod->Name(), "APC")) {
                    producer = prod;
                    break;
                }
                if (prod) prod->Release();
            }

            if (!producer) {
                printf("ERROR: APC Mini MIDI producer not found\n");
                test_midi = false;
            } else {
                MIDILatencyTest consumer;
                consumer.Register();

                if (producer->Connect(&consumer) != B_OK) {
                    printf("ERROR: Failed to connect to MIDI producer\n");
                    test_midi = false;
                } else {
                    if (!consumer.RunLatencyTest(producer, midi_stats)) {
                        printf("WARNING: MIDI API test failed\n");
                    }
                    producer->Disconnect(&consumer);
                }

                consumer.Unregister();
                producer->Release();
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
