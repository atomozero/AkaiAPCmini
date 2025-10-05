// raw_driver_benchmark.cpp
// Direct driver access test - bypasses libmidi completely
// Purpose: Determine if performance issues are in libmidi or midi_usb driver
//
// This test writes directly to /dev/midi/usb/X device files using raw file I/O.
// No libmidi, no libmidi2, no midi_server - just open(), write(), close()
//
// Architecture comparison:
//   MIDI Kit 2: App → libmidi2 → midi_server → libmidi2 → midi_usb → USB
//   MIDI Kit 1: App → libmidi → midi_usb → USB
//   This test:  App → midi_usb → USB (direct file descriptor)
//
// If this test also shows slowness/crashes, the problem is definitively in midi_usb driver.
// If this test is fast, the problem is in libmidi overhead.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>
#include <OS.h>
#include "apc_mini_defs.h"

// Test configuration
#define TEST_BATCH_SIZE 64
#define TEST_ITERATIONS 10

// Global verbosity
enum LogLevel { QUIET = 0, NORMAL = 1, VERBOSE = 2, DEBUG = 3 };
static LogLevel g_log_level = NORMAL;

#define LOG_NORMAL(...) if (g_log_level >= NORMAL) printf(__VA_ARGS__)
#define LOG_VERBOSE(...) if (g_log_level >= VERBOSE) printf(__VA_ARGS__)
#define LOG_DEBUG(...) if (g_log_level >= DEBUG) printf(__VA_ARGS__)

// Statistics tracking
struct TestStats {
    std::vector<bigtime_t> batch_time_samples;
    bigtime_t min_batch_time;
    bigtime_t max_batch_time;
    bigtime_t total_batch_time;
    uint32_t messages_sent;
    uint32_t batches_completed;

    TestStats() { Reset(); }

    void Reset() {
        messages_sent = 0;
        batches_completed = 0;
        min_batch_time = UINT64_MAX;
        max_batch_time = 0;
        total_batch_time = 0;
        batch_time_samples.clear();
    }

    void RecordBatchTime(bigtime_t time) {
        batch_time_samples.push_back(time);
        total_batch_time += time;
        if (time < min_batch_time) min_batch_time = time;
        if (time > max_batch_time) max_batch_time = time;
    }

    double GetAverage() const {
        if (batch_time_samples.empty()) return 0.0;
        return (double)total_batch_time / batch_time_samples.size();
    }

    double GetStdDev() const {
        if (batch_time_samples.size() < 2) return 0.0;
        double avg = GetAverage();
        double sum_sq_diff = 0.0;
        for (bigtime_t sample : batch_time_samples) {
            double diff = sample - avg;
            sum_sq_diff += diff * diff;
        }
        return sqrt(sum_sq_diff / batch_time_samples.size());
    }

    bigtime_t GetPercentile(double p) const {
        if (batch_time_samples.empty()) return 0;
        std::vector<bigtime_t> sorted = batch_time_samples;
        std::sort(sorted.begin(), sorted.end());
        size_t index = (size_t)(p * sorted.size());
        if (index >= sorted.size()) index = sorted.size() - 1;
        return sorted[index];
    }
};

// Find APC Mini device file
const char* FindAPCMiniDevice() {
    static char device_path[256];

    DIR* dir = opendir("/dev/midi/usb");
    if (!dir) {
        LOG_NORMAL("ERROR: Cannot open /dev/midi/usb directory\n");
        return nullptr;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        snprintf(device_path, sizeof(device_path), "/dev/midi/usb/%s", entry->d_name);

        // Try to open and test if it's APC Mini
        // We'll just use the first available device for this test
        // In production, you'd need to query USB device info
        LOG_DEBUG("Found MIDI device: %s\n", device_path);

        closedir(dir);
        return device_path;
    }

    closedir(dir);
    return nullptr;
}

// Send LED command via raw file I/O
bool SendLEDCommand(int fd, uint8_t note, uint8_t color) {
    // APC Mini LED command: Note On on channel 0
    uint8_t midi_msg[3];
    midi_msg[0] = 0x90 | APC_MINI_MIDI_CHANNEL;  // Note On + channel
    midi_msg[1] = note;
    midi_msg[2] = color;

    LOG_DEBUG("Writing MIDI: [%02X %02X %02X]\n", midi_msg[0], midi_msg[1], midi_msg[2]);

    ssize_t written = write(fd, midi_msg, 3);
    if (written != 3) {
        LOG_NORMAL("ERROR: write() returned %ld, errno=%d (%s)\n",
                   written, errno, strerror(errno));
        return false;
    }

    return true;
}

// Test with NO delay (will likely crash if driver has race condition)
bool RunTestNoDelay(int fd, TestStats& stats) {
    LOG_NORMAL("\n=== Test 1: No Delay Between Messages ===\n");
    LOG_NORMAL("This will crash if driver has race condition...\n\n");

    stats.Reset();

    for (int batch = 0; batch < TEST_ITERATIONS; batch++) {
        bigtime_t batch_start = system_time();

        // Send batch as fast as possible
        for (int i = 0; i < TEST_BATCH_SIZE; i++) {
            uint8_t note = i % 64;  // Cycle through pads
            uint8_t color = (i % 3 == 0) ? APC_LED_GREEN :
                           (i % 3 == 1) ? APC_LED_RED : APC_LED_YELLOW;

            if (!SendLEDCommand(fd, note, color)) {
                LOG_NORMAL("ERROR: Failed to send LED command (batch %d, msg %d)\n",
                           batch, i);
                return false;
            }

            stats.messages_sent++;
        }

        bigtime_t batch_time = system_time() - batch_start;
        stats.RecordBatchTime(batch_time);
        stats.batches_completed++;

        LOG_VERBOSE("Batch %2d: %ld μs (%d msgs)\n", batch, batch_time, TEST_BATCH_SIZE);
    }

    LOG_NORMAL("✓ Test completed without crash!\n");
    return true;
}

// Test with 1ms delay (minimal delay)
bool RunTestWithDelay1ms(int fd, TestStats& stats) {
    LOG_NORMAL("\n=== Test 2: 1ms Delay Between Messages ===\n");

    stats.Reset();

    for (int batch = 0; batch < TEST_ITERATIONS; batch++) {
        bigtime_t batch_start = system_time();

        for (int i = 0; i < TEST_BATCH_SIZE; i++) {
            uint8_t note = i % 64;
            uint8_t color = (i % 3 == 0) ? APC_LED_GREEN :
                           (i % 3 == 1) ? APC_LED_RED : APC_LED_YELLOW;

            if (!SendLEDCommand(fd, note, color)) {
                LOG_NORMAL("ERROR: Failed to send LED command\n");
                return false;
            }

            snooze(1000);  // 1ms delay
            stats.messages_sent++;
        }

        bigtime_t batch_time = system_time() - batch_start;
        stats.RecordBatchTime(batch_time);
        stats.batches_completed++;

        LOG_VERBOSE("Batch %2d: %ld μs (%d msgs)\n", batch, batch_time, TEST_BATCH_SIZE);
    }

    LOG_NORMAL("✓ Test completed\n");
    return true;
}

// Test with 5ms delay (known safe delay from midikit_driver_test)
bool RunTestWithDelay5ms(int fd, TestStats& stats) {
    LOG_NORMAL("\n=== Test 3: 5ms Delay Between Messages ===\n");
    LOG_NORMAL("(This is the known-safe delay from libmidi tests)\n\n");

    stats.Reset();

    for (int batch = 0; batch < TEST_ITERATIONS; batch++) {
        bigtime_t batch_start = system_time();

        for (int i = 0; i < TEST_BATCH_SIZE; i++) {
            uint8_t note = i % 64;
            uint8_t color = (i % 3 == 0) ? APC_LED_GREEN :
                           (i % 3 == 1) ? APC_LED_RED : APC_LED_YELLOW;

            if (!SendLEDCommand(fd, note, color)) {
                LOG_NORMAL("ERROR: Failed to send LED command\n");
                return false;
            }

            snooze(5000);  // 5ms delay
            stats.messages_sent++;
        }

        bigtime_t batch_time = system_time() - batch_start;
        stats.RecordBatchTime(batch_time);
        stats.batches_completed++;

        LOG_VERBOSE("Batch %2d: %ld μs (%d msgs)\n", batch, batch_time, TEST_BATCH_SIZE);
    }

    LOG_NORMAL("✓ Test completed\n");
    return true;
}

// Print statistics
void PrintStats(const char* test_name, const TestStats& stats) {
    printf("\n=== %s Results ===\n", test_name);
    printf("Messages sent:     %u\n", stats.messages_sent);
    printf("Batches completed: %u\n", stats.batches_completed);

    if (stats.batch_time_samples.empty()) {
        printf("No timing data collected\n");
        return;
    }

    printf("\nBatch timing:\n");
    printf("  Min:         %ld μs\n",
           stats.min_batch_time == (bigtime_t)UINT64_MAX ? 0 : stats.min_batch_time);
    printf("  P50:    %ld μs  (median)\n", stats.GetPercentile(0.50));
    printf("  Avg:    %.2f μs\n", stats.GetAverage());
    printf("  P95:    %ld μs\n", stats.GetPercentile(0.95));
    printf("  P99:    %ld μs\n", stats.GetPercentile(0.99));
    printf("  Max:    %ld μs\n", stats.max_batch_time);
    printf("  StdDev: %.2f μs\n", stats.GetStdDev());

    // Calculate per-message time
    if (stats.messages_sent > 0) {
        double time_per_msg = stats.GetAverage() / TEST_BATCH_SIZE;
        printf("\nPer-message average: %.2f μs\n", time_per_msg);
    }
}

int main(int argc, char* argv[]) {
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            g_log_level = VERBOSE;
        } else if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            g_log_level = DEBUG;
        } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            g_log_level = QUIET;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Raw Driver Benchmark - Direct /dev/midi/usb access (no libmidi)\n\n");
            printf("Usage: %s [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  --verbose, -v    Show detailed output\n");
            printf("  --debug, -d      Show debug messages\n");
            printf("  --quiet, -q      Minimal output\n");
            printf("  --help, -h       Show this help\n\n");
            printf("Purpose:\n");
            printf("  Test if performance issues are in libmidi or midi_usb driver\n");
            printf("  by bypassing all MIDI Kit libraries and writing directly to\n");
            printf("  /dev/midi/usb device files.\n\n");
            return 0;
        }
    }

    printf("=== Raw Driver Benchmark ===\n");
    printf("Direct /dev/midi/usb access - NO libmidi, NO midi_server\n");
    printf("Architecture: App → write() → midi_usb driver → USB\n\n");

    // Find APC Mini device
    const char* device_path = FindAPCMiniDevice();
    if (!device_path) {
        LOG_NORMAL("ERROR: No MIDI USB devices found in /dev/midi/usb\n");
        LOG_NORMAL("Make sure APC Mini is connected and driver is loaded\n");
        return 1;
    }

    LOG_NORMAL("Using device: %s\n", device_path);

    // Open device with write-only access
    int fd = open(device_path, O_WRONLY);
    if (fd < 0) {
        LOG_NORMAL("ERROR: Cannot open %s: %s\n", device_path, strerror(errno));
        LOG_NORMAL("Check permissions: ls -l %s\n", device_path);
        return 1;
    }

    LOG_NORMAL("✓ Device opened successfully (fd=%d)\n", fd);

    TestStats stats_no_delay, stats_1ms, stats_5ms;
    bool success = true;

    // Test 1: No delay (will likely crash if driver has race condition)
    if (success) {
        success = RunTestNoDelay(fd, stats_no_delay);
        if (success) {
            PrintStats("No Delay Test", stats_no_delay);
        } else {
            LOG_NORMAL("\n❌ CRASH DETECTED - Driver has race condition even without libmidi!\n");
        }
    }

    // Test 2: 1ms delay
    if (success) {
        snooze(500000);  // 500ms pause between tests
        success = RunTestWithDelay1ms(fd, stats_1ms);
        if (success) {
            PrintStats("1ms Delay Test", stats_1ms);
        } else {
            LOG_NORMAL("\n❌ CRASH/ERROR even with 1ms delay\n");
        }
    }

    // Test 3: 5ms delay (should always work)
    if (success) {
        snooze(500000);  // 500ms pause between tests
        RunTestWithDelay5ms(fd, stats_5ms);
        PrintStats("5ms Delay Test", stats_5ms);
    }

    close(fd);

    // Final analysis
    printf("\n=== Analysis ===\n");

    if (!success) {
        printf("❌ DRIVER HAS RACE CONDITION\n");
        printf("   The crash occurs even with direct write() calls,\n");
        printf("   proving the bug is in midi_usb driver, not libmidi.\n\n");
        printf("Recommendation: Use USB Raw access to bypass broken driver\n");
    } else {
        printf("✓ All tests completed\n\n");

        printf("Performance comparison (avg per-message time):\n");
        printf("  No delay:  %.2f μs/msg\n", stats_no_delay.GetAverage() / TEST_BATCH_SIZE);
        printf("  1ms delay: %.2f μs/msg\n", stats_1ms.GetAverage() / TEST_BATCH_SIZE);
        printf("  5ms delay: %.2f μs/msg\n", stats_5ms.GetAverage() / TEST_BATCH_SIZE);

        printf("\nComparison with libmidi approaches:\n");
        printf("  MIDI Kit 2 (virtual):   ~7.65 μs/msg\n");
        printf("  MIDI Kit 1 (BMidiPort): ~5000 μs/msg (with 5ms delay workaround)\n");
        printf("  Raw driver (this test): %.2f μs/msg (no delay)\n",
               stats_no_delay.GetAverage() / TEST_BATCH_SIZE);

        if (stats_no_delay.GetAverage() / TEST_BATCH_SIZE < 100) {
            printf("\n✓ Direct driver access is FAST - libmidi overhead is the bottleneck\n");
        } else {
            printf("\n⚠️ Direct driver access is also slow - driver itself is slow\n");
        }
    }

    return success ? 0 : 1;
}
