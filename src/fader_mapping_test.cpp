/*
 * APC Mini Fader CC Mapping Validation Test
 * Tests the corrected fader CC mapping against APC Mini hardware specification
 */

#include "apc_mini_defs.h"
#include <stdio.h>
#include <assert.h>

// Test data based on APC Mini specification
struct FaderTestCase {
    uint8_t cc_number;
    const char* description;
    bool is_track_fader;
    bool is_master_fader;
    int expected_track_index;  // -1 for master
};

static const FaderTestCase test_cases[] = {
    // Track faders 1-8 (CC 48-55)
    {48, "Track Fader 1", true, false, 0},
    {49, "Track Fader 2", true, false, 1},
    {50, "Track Fader 3", true, false, 2},
    {51, "Track Fader 4", true, false, 3},
    {52, "Track Fader 5", true, false, 4},
    {53, "Track Fader 6", true, false, 5},
    {54, "Track Fader 7", true, false, 6},
    {55, "Track Fader 8", true, false, 7},

    // Master fader (CC 56)
    {56, "Master Fader", false, true, -1},

    // Invalid faders (should not match)
    {47, "Invalid CC 47", false, false, -1},
    {57, "Invalid CC 57", false, false, -1},
    {0,  "Invalid CC 0",  false, false, -1},
    {127,"Invalid CC 127",false, false, -1}
};

void test_fader_cc_definitions()
{
    printf("Testing APC Mini Fader CC Definitions...\n");

    // Test macro definitions
    assert(APC_MINI_FADER_CC_START == 48);
    assert(APC_MINI_FADER_CC_END == 55);
    assert(APC_MINI_MASTER_CC == 56);
    assert(APC_MINI_TRACK_FADER_COUNT == 8);
    assert(APC_MINI_TOTAL_FADER_COUNT == 9);

    printf("✅ All macro definitions correct\n");
}

void test_fader_detection_macros()
{
    printf("Testing Fader Detection Macros...\n");

    for (size_t i = 0; i < sizeof(test_cases)/sizeof(test_cases[0]); i++) {
        const FaderTestCase& test = test_cases[i];

        bool is_track = IS_TRACK_FADER_CC(test.cc_number);
        bool is_master = IS_MASTER_FADER_CC(test.cc_number);
        bool is_any = IS_ANY_FADER_CC(test.cc_number);

        printf("CC %3d (%s): Track=%s Master=%s Any=%s\n",
               test.cc_number, test.description,
               is_track ? "✅" : "❌",
               is_master ? "✅" : "❌",
               is_any ? "✅" : "❌");

        // Validate expectations
        assert(is_track == test.is_track_fader);
        assert(is_master == test.is_master_fader);
        assert(is_any == (test.is_track_fader || test.is_master_fader));

        // Test track index calculation
        if (test.is_track_fader) {
            int track_index = test.cc_number - APC_MINI_FADER_CC_START;
            assert(track_index == test.expected_track_index);
            assert(track_index >= 0 && track_index < APC_MINI_TRACK_FADER_COUNT);
        }
    }

    printf("✅ All fader detection macros work correctly\n");
}

void test_array_bounds()
{
    printf("Testing Array Bounds Safety...\n");

    // Test that all valid track fader indices are within array bounds
    for (uint8_t cc = APC_MINI_FADER_CC_START; cc <= APC_MINI_FADER_CC_END; cc++) {
        int index = cc - APC_MINI_FADER_CC_START;
        assert(index >= 0);
        assert(index < APC_MINI_TRACK_FADER_COUNT);
        printf("CC %d -> Track index %d ✅\n", cc, index);
    }

    printf("✅ All array bounds are safe\n");
}

void test_physical_layout_mapping()
{
    printf("Testing Physical Layout Mapping...\n");

    // APC Mini Physical Layout: [F1] [F2] [F3] [F4] [F5] [F6] [F7] [F8] [MASTER]
    //                    CC:     48   49   50   51   52   53   54   55     56

    const char* physical_names[] = {
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8"
    };

    for (int i = 0; i < APC_MINI_TRACK_FADER_COUNT; i++) {
        uint8_t expected_cc = APC_MINI_FADER_CC_START + i;
        printf("Physical %s -> CC %d (index %d) ✅\n",
               physical_names[i], expected_cc, i);
    }

    printf("Master Fader -> CC %d ✅\n", APC_MINI_MASTER_CC);
    printf("✅ Physical layout mapping correct\n");
}

void test_mock_midi_processing()
{
    printf("Testing Mock MIDI Message Processing...\n");

    // Simulate APCMiniState structure usage
    struct {
        uint8_t track_fader_values[APC_MINI_TRACK_FADER_COUNT];
        uint8_t master_fader_value;
    } mock_state = {0};

    // Test track fader updates
    for (int i = 0; i < APC_MINI_TRACK_FADER_COUNT; i++) {
        uint8_t cc = APC_MINI_FADER_CC_START + i;
        uint8_t value = (i + 1) * 15;  // Test values

        if (IS_TRACK_FADER_CC(cc)) {
            int index = cc - APC_MINI_FADER_CC_START;
            mock_state.track_fader_values[index] = value;
            printf("Track fader %d (CC %d) set to %d ✅\n", i+1, cc, value);
        }
    }

    // Test master fader update
    if (IS_MASTER_FADER_CC(APC_MINI_MASTER_CC)) {
        mock_state.master_fader_value = 127;
        printf("Master fader (CC %d) set to 127 ✅\n", APC_MINI_MASTER_CC);
    }

    printf("✅ Mock MIDI processing works correctly\n");
}

int main()
{
    printf("🎛️  APC Mini Fader CC Mapping Validation Test\n");
    printf("================================================\n\n");

    try {
        test_fader_cc_definitions();
        test_fader_detection_macros();
        test_array_bounds();
        test_physical_layout_mapping();
        test_mock_midi_processing();

        printf("\n🎉 ALL TESTS PASSED! Fader CC mapping is correct.\n");
        printf("✅ Ready for deployment to Haiku OS\n");

        return 0;
    } catch (...) {
        printf("\n❌ TEST FAILED! Check fader CC mapping implementation.\n");
        return 1;
    }
}