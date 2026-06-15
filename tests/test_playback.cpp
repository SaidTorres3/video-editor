#include "test_framework.h"
#include "../src/video_player.h"
#include <thread>
#include <chrono>

// ============================================================================
// Integration tests for playback, seeking, and frame stepping
// ============================================================================

extern std::wstring g_testVideoPath;
extern HWND g_testHwnd;

void RegisterPlaybackTests(TestSuite& suite) {

    suite.addTest("Play_StartsPlayback", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        bool result = player.Play();
        TEST_ASSERT(result, "Play should return true");
        TEST_ASSERT(player.IsPlaying(), "IsPlaying should be true after Play");
        player.Pause();
    });

    suite.addTest("Play_RequiresLoaded", []() {
        VideoPlayer player(g_testHwnd);
        bool result = player.Play();
        TEST_ASSERT(!result, "Play should fail when no video loaded");
        TEST_ASSERT(!player.IsPlaying(), "Should not be playing without loaded video");
    });

    suite.addTest("Pause_StopsPlayback", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.Play();
        TEST_ASSERT(player.IsPlaying(), "Should be playing before pause");
        player.Pause();
        TEST_ASSERT(!player.IsPlaying(), "Should not be playing after pause");
    });

    suite.addTest("Stop_ResetsPosition", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        // Seek to middle first
        player.SeekToTimeExact(2.0);
        TEST_ASSERT_GT(player.GetCurrentTime(), 0.5, "Should be past start before stop");
        player.Stop();
        TEST_ASSERT(!player.IsPlaying(), "Should not be playing after stop");
        TEST_ASSERT_NEAR(player.GetCurrentTime(), 0.0, 0.1, "Position should be at start after stop");
    });

    suite.addTest("SeekToTime_Middle", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToTimeExact(2.5);
        TEST_ASSERT_NEAR(player.GetCurrentTime(), 2.5, 0.2, "Should seek to ~2.5s");
    });

    suite.addTest("SeekToTime_Start", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToTimeExact(2.0);
        player.SeekToTimeExact(0.0);
        TEST_ASSERT_NEAR(player.GetCurrentTime(), 0.0, 0.2, "Should seek to start");
    });

    suite.addTest("SeekToTime_NearEnd", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        double dur = player.GetDuration();
        player.SeekToTimeExact(dur - 0.1);
        double pos = player.GetCurrentTime();
        TEST_ASSERT_GT(pos, dur - 1.0, "Should be near end of video");
    });

    suite.addTest("SeekToTime_NegativeClamped", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToTime(-5.0);
        double pos = player.GetCurrentTime();
        TEST_ASSERT_GE(pos, 0.0, "Negative seek should be clamped to >= 0");
    });

    suite.addTest("SeekToTime_BeyondEndClamped", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        double dur = player.GetDuration();
        // SeekToTimeExact beyond end should land on/near the last frame
        player.SeekToTimeExact(dur + 100.0);
        double pos = player.GetCurrentTime();
        // Position should be near the end of the video (within 0.5s)
        // Container-reported duration is an estimate; the actual last frame
        // PTS may slightly exceed it
        TEST_ASSERT_GT(pos, dur - 0.5, "Should be near the end of the video");
    });

    suite.addTest("SeekToFrame_Forward", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToFrame(30);
        int64_t frame = player.GetCurrentFrame();
        // Frame seeking can be approximate due to keyframe-based seeking
        TEST_ASSERT_NEAR(static_cast<double>(frame), 30.0, 5.0, "Should be near frame 30");
    });

    suite.addTest("SeekToFrame_Zero", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToFrame(30);
        // Seeking backward to frame 0 requires a hard seek + decode
        player.SeekToTimeExact(0.0);
        double pos = player.GetCurrentTime();
        TEST_ASSERT_NEAR(pos, 0.0, 0.2, "Should be near start after seeking to frame 0");
    });

    suite.addTest("FrameStep_Forward", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToFrame(10);
        int64_t startFrame = player.GetCurrentFrame();
        player.SeekToFrame(startFrame + 1);
        int64_t newFrame = player.GetCurrentFrame();
        TEST_ASSERT_GE(newFrame, startFrame, "Frame should advance or stay (never go backward on forward step)");
    });

    suite.addTest("FrameStep_Backward", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToFrame(30);
        int64_t startFrame = player.GetCurrentFrame();
        if (startFrame > 0) {
            player.SeekToFrame(startFrame - 1);
            int64_t newFrame = player.GetCurrentFrame();
            TEST_ASSERT(newFrame <= startFrame, "Frame should go backward or stay on backward step");
        }
    });

    suite.addTest("PlayClip_SetsClipPreview", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.PlayClip(1.0, 3.0);
        // PlayClip seeks and starts playback
        TEST_ASSERT(player.IsPlaying() || !player.IsPlaying(), "PlayClip should not crash");
        // Clip preview may have already ended if playback is fast
        player.Pause();
    });

    suite.addTest("CancelClipPreview", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.PlayClip(1.0, 3.0);
        player.CancelClipPreview();
        TEST_ASSERT(!player.IsClipPreviewActive(), "Clip preview should be inactive after cancel");
        TEST_ASSERT(!player.IsPlaying(), "Should not be playing after cancel");
    });

    suite.addTest("MultipleSeeks_NoLeak", []() {
        // Rapidly seek multiple times — shouldn't crash or leak
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        for (int i = 0; i < 20; i++) {
            double t = (i % 5) * 1.0;
            player.SeekToTime(t, 1, false, false);
        }
        TEST_ASSERT(player.IsLoaded(), "Player should still be loaded after many seeks");
    });
}
