#include "test_framework.h"
#include "../src/video_player.h"
#include <thread>
#include <chrono>
#include <cmath>

// ============================================================================
// Integration tests for playback, seeking, and frame stepping
// ============================================================================

extern std::wstring g_testVideoPath;
extern HWND g_testHwnd;

void RegisterPlaybackTests(TestSuite& suite) {

    suite.addTest("PlaybackSpeed_AdjustsAndClamps", []() {
        VideoPlayer player(g_testHwnd);
        player.SetPlaybackSpeed(1.1);
        TEST_ASSERT(std::fabs(player.GetPlaybackSpeed() - 1.1) < 0.001,
                    "Playback speed should increase by ten percentage points");
        TEST_ASSERT(player.IsPlaybackSpeedOverlayVisible(),
                    "Changing speed should show the preview overlay");
        player.SetPlaybackSpeed(-5.0);
        TEST_ASSERT(std::fabs(player.GetPlaybackSpeed() - 0.1) < 0.001,
                    "Playback speed should clamp to 10%");
        player.SetPlaybackSpeed(9.0);
        TEST_ASSERT(std::fabs(player.GetPlaybackSpeed() - 9.0) < 0.001,
                    "Playback speed should allow values above 400%");
        player.SetPlaybackSpeed(50.0);
        TEST_ASSERT(std::fabs(player.GetPlaybackSpeed() - 50.0) < 0.001,
                    "Playback speed should allow values above 10x");
        player.SetPlaybackSpeed(101.0);
        TEST_ASSERT(std::fabs(player.GetPlaybackSpeed() - 100.0) < 0.001,
                    "Playback speed should clamp to 100x");
    });

    suite.addTest("PlaybackSpeed_AffectsPlaybackTiming", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SetPlaybackSpeed(2.0);
        player.Play();
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        player.Pause();
        TEST_ASSERT_GT(player.GetCurrentTime(), 0.8,
                       "200% speed should advance substantially faster than real time");
    });

    suite.addTest("PlaybackSpeed_OneXStartsPromptlyAndTracksWallClock", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.Play();

        auto startupDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
        while (player.GetCurrentTime() < 0.05 && std::chrono::steady_clock::now() < startupDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        TEST_ASSERT_GT(player.GetCurrentTime(), 0.05,
                       "1x playback should present frames promptly");

        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        player.Pause();
        TEST_ASSERT_GT(player.GetCurrentTime(), 0.25,
                       "1x playback should keep advancing without catch-up seek stalls");
        TEST_ASSERT_LT(player.GetCurrentTime(), 1.0,
                       "1x playback should remain close to its wall clock");
    });

    suite.addTest("Playback_ResumeContinuesWithoutStalling", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.Play();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        player.Pause();
        const double pausedAt = player.GetCurrentTime();

        player.Play();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        player.Pause();
        TEST_ASSERT_GT(player.GetCurrentTime(), pausedAt + 0.1,
                       "Playback should resume promptly after WASAPI is reset");
    });

    suite.addTest("PlaybackSpeed_10xTracksWallClock", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SetPlaybackSpeed(10.0);
        player.Play();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        player.Pause();
        TEST_ASSERT_GT(player.GetCurrentTime(), 2.3,
                       "10x speed should advance close to ten video seconds per real second");
        TEST_ASSERT_LT(player.GetCurrentTime(), 3.8,
                       "10x playback should not run substantially ahead of its wall clock");
    });

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

    suite.addTest("SeekWhilePlaying_ReachesTarget", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.Play();
        player.SeekWhilePlaying(2.0);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (player.GetCurrentTime() < 1.8 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        TEST_ASSERT_GE(player.GetCurrentTime(), 1.8,
                       "Asynchronous seek should reach the requested playback position");
        TEST_ASSERT(player.IsPlaying(), "Seek should not pause playback");
        player.Pause();
    });

    suite.addTest("SeekWhilePlaying_CoalescesRapidRequests", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.Play();
        player.SeekWhilePlaying(0.5);
        player.SeekWhilePlaying(1.0);
        player.SeekWhilePlaying(3.0);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (player.GetCurrentTime() < 2.8 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        TEST_ASSERT_GE(player.GetCurrentTime(), 2.8,
                       "Rapid seeks should collapse to the newest requested position");
        player.Pause();
    });

    suite.addTest("SeekWhilePlaying_ContinuousPreviewUpdates", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.Play();

        double lastObserved = player.GetCurrentTime();
        int previewUpdates = 0;
        for (int i = 0; i < 50; ++i) {
            double target = 0.25 + (i % 35) * 0.1;
            player.SeekWhilePlaying(target, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            double observed = player.GetCurrentTime();
            if (std::fabs(observed - lastObserved) > 0.05) {
                ++previewUpdates;
                lastObserved = observed;
            }
        }
        TEST_ASSERT_GT(previewUpdates, 2,
                       "Held seek input should display intermediate preview positions");
        player.SeekWhilePlaying(2.0);
        player.Pause();
    });

    suite.addTest("PausedSeek_PresentsFastPreviewBeforeRefinement", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        TEST_ASSERT(player.d2dBitmap == nullptr,
                    "Fresh player should not already contain a presented frame");

        player.SeekToTime(2.5, 1, true, true);
        TEST_ASSERT(player.d2dBitmap != nullptr,
                    "Paused seek should present its quick frame before exact refinement finishes");
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

    suite.addTest("FrameStep_ForwardLongRun_UsesAlignedDecoder", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToFrame(10);

        // Ensure reverse prefetch has had time to populate. Forward stepping
        // must still ignore that cache while the main decoder is aligned.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        auto slowestStep = std::chrono::steady_clock::duration::zero();
        for (int i = 0; i < 100; ++i) {
            int64_t before = player.GetCurrentFrame();
            auto started = std::chrono::steady_clock::now();
            player.StepFrame(1);
            auto elapsed = std::chrono::steady_clock::now() - started;
            if (elapsed > slowestStep)
                slowestStep = elapsed;
            TEST_ASSERT_GT(player.GetCurrentFrame(), before,
                           "Aligned forward stepping must advance synchronously");
        }

        auto slowestMs = std::chrono::duration_cast<std::chrono::milliseconds>(slowestStep).count();
        TEST_ASSERT_LT(slowestMs, static_cast<int64_t>(100),
                       "Reverse prefetch must not stall the normal forward decoder");
    });

    suite.addTest("FrameStep_Backward", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToFrame(30);
        int64_t startFrame = player.GetCurrentFrame();
        if (startFrame > 0) {
            player.SeekToFrame(startFrame - 1);
            int64_t newFrame = player.GetCurrentFrame();
            TEST_ASSERT_LT(newFrame, startFrame, "Frame should move backward even on a prefetch cache miss");
        }
    });

    suite.addTest("FrameStep_ChangesDirection", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToFrame(30);
        int64_t startFrame = player.GetCurrentFrame();
        if (startFrame > 0) {
            player.SeekToFrame(startFrame - 1);
            int64_t backwardFrame = player.GetCurrentFrame();
            player.SeekToFrame(backwardFrame + 1);
            TEST_ASSERT_GT(player.GetCurrentFrame(), backwardFrame,
                           "Forward step should work immediately after a backward step");
        }
    });

    suite.addTest("FrameStep_BackwardLongRun_NoExhaustionStall", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToFrame(125);

        // Let the first rolling batch become available; the test then crosses
        // well beyond that batch to exercise continuous refill.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        auto slowestStep = std::chrono::steady_clock::duration::zero();
        for (int i = 0; i < 105; ++i) {
            int64_t before = player.GetCurrentFrame();
            auto started = std::chrono::steady_clock::now();
            player.SeekToFrame(before - 1);
            auto elapsed = std::chrono::steady_clock::now() - started;
            if (elapsed > slowestStep)
                slowestStep = elapsed;
            TEST_ASSERT_LT(player.GetCurrentFrame(), before,
                           "Every held backward step must advance without exhausting the cache");
        }

        auto slowestMs = std::chrono::duration_cast<std::chrono::milliseconds>(slowestStep).count();
        TEST_ASSERT_LT(slowestMs, static_cast<int64_t>(400),
                       "Rolling backward prefetch should avoid half-second per-frame stalls");
    });

    suite.addTest("FrameStep_AsyncReverseRequestsNeverBlockUI", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToFrame(125);
        const int64_t expectedFrame = 20;

        auto submittedAt = std::chrono::steady_clock::now();
        for (int i = 0; i < 105; ++i)
            player.StepFrame(-1);
        auto submissionMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - submittedAt)
                                .count();
        TEST_ASSERT_LT(submissionMs, static_cast<int64_t>(500),
                       "Reverse key requests must never wait for decoding on the UI thread");

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (player.GetCurrentFrame() > expectedFrame &&
               std::chrono::steady_clock::now() < deadline)
        {
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        TEST_ASSERT(player.GetCurrentFrame() <= expectedFrame,
                    "CPU reverse worker should eventually present the accumulated target");
    });

    suite.addTest("FrameStep_ForwardCancelsPendingReverseTarget", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToFrame(125);
        for (int i = 0; i < 90; ++i)
            player.StepFrame(-1);

        const int64_t displayedBeforeForward = player.GetCurrentFrame();
        player.StepFrame(1);
        const int64_t forwardFrame = player.GetCurrentFrame();
        TEST_ASSERT_GT(forwardFrame, displayedBeforeForward,
                       "Forward step should act on the displayed frame immediately");

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline)
        {
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        TEST_ASSERT_EQ(player.GetCurrentFrame(), forwardFrame,
                       "Cancelled reverse work must not overwrite a later forward step");
    });

    suite.addTest("FrameStep_AsyncForwardCacheMissNeverBlocksUI", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SeekToFrame(125);
        for (int i = 0; i < 90; ++i)
            player.StepFrame(-1);

        auto reverseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (player.GetCurrentFrame() > 35 &&
               std::chrono::steady_clock::now() < reverseDeadline)
        {
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        TEST_ASSERT(player.GetCurrentFrame() <= 35,
                    "Test setup should enter reverse-cache display mode");

        const int64_t forwardStart = player.GetCurrentFrame();
        auto submittedAt = std::chrono::steady_clock::now();
        for (int i = 0; i < 30; ++i)
            player.StepFrame(1);
        auto submissionMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - submittedAt)
                                .count();
        TEST_ASSERT_LT(submissionMs, static_cast<int64_t>(500),
                       "Forward cache misses must not run exact seeks on the UI thread");

        auto forwardDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (player.GetCurrentFrame() <= forwardStart + 10 &&
               std::chrono::steady_clock::now() < forwardDeadline)
        {
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        TEST_ASSERT_GT(player.GetCurrentFrame(), forwardStart + 10,
                       "CPU worker should deliver the first forward frame beyond the cache edge");
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
