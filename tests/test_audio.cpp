#include "test_framework.h"
#include "../src/video_player.h"

// ============================================================================
// Integration tests for audio track management
// ============================================================================

extern std::wstring g_testVideoPath;
extern HWND g_testHwnd;

void RegisterAudioTests(TestSuite& suite) {

    suite.addTest("AudioTrack_DefaultUnmuted", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            TEST_ASSERT(!player.IsAudioTrackMuted(0), "Default track should not be muted");
        }
    });

    suite.addTest("AudioTrack_Mute", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            player.SetAudioTrackMuted(0, true);
            TEST_ASSERT(player.IsAudioTrackMuted(0), "Track should be muted after SetAudioTrackMuted(true)");
        }
    });

    suite.addTest("AudioTrack_Unmute", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            player.SetAudioTrackMuted(0, true);
            player.SetAudioTrackMuted(0, false);
            TEST_ASSERT(!player.IsAudioTrackMuted(0), "Track should be unmuted after SetAudioTrackMuted(false)");
        }
    });

    suite.addTest("AudioTrack_Volume_Default", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            float vol = player.GetAudioTrackVolume(0);
            TEST_ASSERT_NEAR(vol, 1.0f, 0.01f, "Default volume should be 1.0");
        }
    });

    suite.addTest("AudioTrack_Volume_Set", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            player.SetAudioTrackVolume(0, 1.5f);
            float vol = player.GetAudioTrackVolume(0);
            TEST_ASSERT_NEAR(vol, 1.5f, 0.01f, "Volume should be 1.5 after set");
        }
    });

    suite.addTest("AudioTrack_Volume_Zero", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            player.SetAudioTrackVolume(0, 0.0f);
            float vol = player.GetAudioTrackVolume(0);
            TEST_ASSERT_NEAR(vol, 0.0f, 0.01f, "Volume should be 0 after set to 0");
        }
    });

    suite.addTest("AudioTrack_Volume_Negative", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            player.SetAudioTrackVolume(0, -0.5f);
            float vol = player.GetAudioTrackVolume(0);
            TEST_ASSERT_NEAR(vol, 0.0f, 0.01f, "Negative volume should be clamped to 0");
        }
    });

    suite.addTest("AudioTrack_Volume_SubAudible", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            // 0.01 is below the -30dB threshold (0.03162278)
            player.SetAudioTrackVolume(0, 0.01f);
            float vol = player.GetAudioTrackVolume(0);
            TEST_ASSERT_NEAR(vol, 0.0f, 0.01f, "Sub-audible volume should be clamped to 0");
        }
    });

    suite.addTest("AudioTrack_Volume_MaxRange", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            player.SetAudioTrackVolume(0, 2.0f);
            float vol = player.GetAudioTrackVolume(0);
            TEST_ASSERT_NEAR(vol, 2.0f, 0.01f, "200% volume should be accepted");
        }
    });

    suite.addTest("AudioTrack_Name", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            std::string name = player.GetAudioTrackName(0);
            // Name can be empty or not, but calling it shouldn't crash
            TEST_ASSERT(true, "GetAudioTrackName should not crash");
        }
    });

    suite.addTest("AudioTrack_InvalidIndex_Mute", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        // These should not crash
        player.SetAudioTrackMuted(-1, true);
        player.SetAudioTrackMuted(999, true);
        bool result = player.IsAudioTrackMuted(-1);
        TEST_ASSERT(!result, "Invalid index mute query should return false");
    });

    suite.addTest("AudioTrack_InvalidIndex_Volume", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        // These should not crash
        player.SetAudioTrackVolume(-1, 1.0f);
        player.SetAudioTrackVolume(999, 1.0f);
        float vol = player.GetAudioTrackVolume(-1);
        TEST_ASSERT_NEAR(vol, 0.0f, 0.01f, "Invalid index volume should return 0");
    });

    suite.addTest("VoiceIsolation_Enable", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            player.SetVoiceIsolationEnabled(0, true);
            bool enabled = player.IsVoiceIsolationEnabled(0);
            TEST_ASSERT(enabled, "Voice isolation should be enabled");
            // Clean up
            player.SetVoiceIsolationEnabled(0, false);
        }
    });

    suite.addTest("VoiceIsolation_Disable", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        if (player.GetAudioTrackCount() > 0) {
            player.SetVoiceIsolationEnabled(0, true);
            player.SetVoiceIsolationEnabled(0, false);
            bool enabled = player.IsVoiceIsolationEnabled(0);
            TEST_ASSERT(!enabled, "Voice isolation should be disabled");
        }
    });

    suite.addTest("VoiceIsolation_InvalidIndex", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        // Should not crash
        player.SetVoiceIsolationEnabled(-1, true);
        player.SetVoiceIsolationEnabled(999, true);
        bool result = player.IsVoiceIsolationEnabled(-1);
        TEST_ASSERT(!result, "Invalid index should return false");
    });

    suite.addTest("MasterVolume_Set", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        // Should not crash; we can't easily read back master volume
        // but we verify it doesn't throw
        player.SetMasterVolume(0.5f);
        player.SetMasterVolume(1.0f);
        player.SetMasterVolume(2.0f);
        TEST_ASSERT(true, "SetMasterVolume should not crash");
    });
}
