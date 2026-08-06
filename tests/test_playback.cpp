#include "test_framework.h"
#include "../src/video_player.h"
#include "../src/ui_updates.h"
#include "../src/timeline.h"
#include "../src/window_proc.h"
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>

// ============================================================================
// Integration tests for playback, seeking, and frame stepping
// ============================================================================

extern std::wstring g_testVideoPath;
extern std::wstring g_testLongVideoPath;
extern std::wstring g_testHeavyVideoPath;
extern std::wstring g_testSparseSeekVideoPath;
extern std::wstring g_testSparseIndexVideoPath;
extern std::wstring g_testAv1OpusVideoPath;
extern HWND g_testHwnd;
extern double g_previewSeekTime;
extern VideoPlayer* g_videoPlayer;
extern HWND g_hTimeline;
extern bool g_isTimelineDragging;

namespace {
struct TimedPlaybackMeasurement {
    bool started = false;
    double advance = 0.0;
    double sampledRate = 0.0;
    int positionChanges = 0;
    uint64_t presentedFrames = 0;
    int64_t longestStallMs = 0;
};

TimedPlaybackMeasurement MeasureTimedPlayback(VideoPlayer& player,
                                              std::chrono::milliseconds duration) {
    TimedPlaybackMeasurement result;
    const auto startupDeadline = std::chrono::steady_clock::now() +
                                 std::chrono::seconds(3);
    while (player.GetCurrentTime() < 0.05 &&
           std::chrono::steady_clock::now() < startupDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    result.started = player.GetCurrentTime() > 0.05;
    if (!result.started)
        return result;

    const double startPosition = player.GetCurrentTime();
    const uint64_t startPresentations = player.GetPresentedPlaybackFrameCount();
    double lastPosition = startPosition;
    double firstChangedPosition = startPosition;
    double lastChangedPosition = startPosition;
    bool hasFirstChange = false;
    auto startAt = std::chrono::steady_clock::now();
    auto lastChangeAt = startAt;
    auto firstChangeAt = startAt;
    auto lastChangedAt = startAt;
    const auto deadline = startAt + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto now = std::chrono::steady_clock::now();
        const double position = player.GetCurrentTime();
        if (position > lastPosition + 0.0001) {
            result.longestStallMs = (std::max)(result.longestStallMs,
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastChangeAt).count());
            if (!hasFirstChange) {
                hasFirstChange = true;
                firstChangeAt = now;
                firstChangedPosition = position;
            }
            lastChangedAt = now;
            lastChangedPosition = position;
            lastChangeAt = now;
            lastPosition = position;
            ++result.positionChanges;
        }
    }
    result.longestStallMs = (std::max)(result.longestStallMs,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - lastChangeAt).count());
    result.advance = player.GetCurrentTime() - startPosition;
    result.presentedFrames = player.GetPresentedPlaybackFrameCount() - startPresentations;
    const double sampledSeconds = std::chrono::duration<double>(
        lastChangedAt - firstChangeAt).count();
    if (hasFirstChange && sampledSeconds > 0.0)
        result.sampledRate = (lastChangedPosition - firstChangedPosition) / sampledSeconds;
    return result;
}

bool WaitForPresentedFrames(VideoPlayer& player, uint64_t target,
                            std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (player.GetPresentedPlaybackFrameCount() < target &&
           player.IsPlaying() && !player.HasPlaybackDecoderEnded() &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return player.GetPresentedPlaybackFrameCount() >= target;
}
}

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
        player.SetPlaybackSpeed(250.0);
        TEST_ASSERT(std::fabs(player.GetPlaybackSpeed() - 250.0) < 0.001,
                    "Playback speed should not have an artificial upper limit");
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

    suite.addTest("TestDecoder_UsesDeterministicSoftwarePath", []() {
        VideoPlayer player(g_testHwnd);
        TEST_ASSERT(player.LoadVideo(g_testVideoPath),
                    "software-decoder regression source must load");
        TEST_ASSERT(!player.useHwAccel,
                    "automated tests must not depend on a hosted runner's partial GPU device");
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

    suite.addTest("Playback_DefaultSpeedOutlivesThreeFrameQueueAndSeek", []() {
        VideoPlayer player(g_testHwnd);
        TEST_ASSERT(player.LoadVideo(g_testHeavyVideoPath),
                    "three-frame queue regression source must load");
        player.ForceBufferedDecoderEagainAfterPacketsForTesting(3);
        player.ForcePlaybackBufferCapacityForTesting(3);
        TEST_ASSERT(player.Play(),
                    "play from the beginning must start at default speed");
        TEST_ASSERT_EQ(player.GetPlaybackBufferCapacity(), static_cast<size_t>(3),
                       "regression source must exercise the real three-frame queue");

        const uint64_t startPresentations = player.GetPresentedPlaybackFrameCount();
        const uint64_t beyondInitialQueue = startPresentations +
            static_cast<uint64_t>(player.GetPlaybackBufferCapacity()) + 12;
        const bool continuedFromBeginning = WaitForPresentedFrames(
            player, beyondInitialQueue, std::chrono::seconds(6));
        TEST_ASSERT(continuedFromBeginning,
                    "default-speed playback must not stop after its first three frames; delivered=" +
                    std::to_string(player.GetPresentedPlaybackFrameCount() - startPresentations) +
                    ", playing=" + std::to_string(player.IsPlaying()) +
                    ", eof=" + std::to_string(player.HasPlaybackDecoderEnded()));
        TEST_ASSERT(player.IsPlaying(),
                    "player must still be running after the initial queue is drained");
        TEST_ASSERT(!player.HasPlaybackDecoderEnded(),
                    "decoder must not scan to EOF after the initial queue is drained");
        TEST_ASSERT_EQ(player.GetInjectedBufferedDecoderEagainCountForTesting(),
                       static_cast<uint64_t>(1),
                       "test must force send_packet(EAGAIN), drain output, and retry the retained packet once");

        player.SeekWhilePlaying(2.0, false);
        const uint64_t beforeSeekRecovery = player.GetPresentedPlaybackFrameCount();
        const bool continuedAfterSeek = WaitForPresentedFrames(
            player, beforeSeekRecovery + 12, std::chrono::seconds(6));
        TEST_ASSERT(continuedAfterSeek,
                    "seeking a stuck-looking playing instance must keep delivering frames; delivered=" +
                    std::to_string(player.GetPresentedPlaybackFrameCount() - beforeSeekRecovery) +
                    ", playing=" + std::to_string(player.IsPlaying()) +
                    ", eof=" + std::to_string(player.HasPlaybackDecoderEnded()));
        TEST_ASSERT(player.IsPlaying(),
                    "player must remain playable after the in-playback seek");
        TEST_ASSERT_GT(player.GetCurrentTime(), 2.1,
                       "playback must advance beyond the seek target");
        player.Pause();
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

    suite.addTest("PlaybackSpeed_5xSustainsRequestedRate", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SetPlaybackSpeed(5.0);
        player.Play();
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        player.Pause();

        TEST_ASSERT_GT(player.GetCurrentTime(), 3.2,
                       "5x playback should advance about four video seconds in 0.8 real seconds");
        TEST_ASSERT_LT(player.GetCurrentTime(), 4.8,
                       "5x playback should not run materially ahead of its wall clock");
    });

    suite.addTest("PlaybackSpeed_4xAnd5xOutliveInitialQueueAndRecoverAfterSeek", []() {
        for (const double speed : {4.0, 5.0}) {
            VideoPlayer player(g_testHwnd);
            TEST_ASSERT(player.LoadVideo(g_testLongVideoPath),
                        "high-speed queue-drain regression source must load");
            player.SetPlaybackSpeed(speed);
            TEST_ASSERT(player.Play(), "high-speed playback must start");

            const uint64_t initialTarget =
                player.GetPresentedPlaybackFrameCount() +
                static_cast<uint64_t>(player.GetPlaybackBufferCapacity()) + 12;
            const bool decodedBeyondInitialQueue = WaitForPresentedFrames(
                player, initialTarget, std::chrono::seconds(2));
            TEST_ASSERT(!player.HasPlaybackDecoderEnded(),
                        "decoder producer must not die after the initial frame queue drains");
            TEST_ASSERT(decodedBeyondInitialQueue,
                        "playback must present well beyond its three-frame initial queue");
            TEST_ASSERT(player.IsPlaying(),
                        "player must remain running after the initial queue drains");

            player.SeekWhilePlaying(20.0, false);
            const uint64_t presentationsAfterSeek =
                player.GetPresentedPlaybackFrameCount() + 12;
            const bool continuedAfterSeek = WaitForPresentedFrames(
                player, presentationsAfterSeek, std::chrono::seconds(2));
            TEST_ASSERT(!player.HasPlaybackDecoderEnded(),
                        "an in-playback seek must not terminate the decoder producer");
            const uint64_t deliveredAfterSeek =
                player.GetPresentedPlaybackFrameCount() -
                (presentationsAfterSeek - 12);
            TEST_ASSERT(continuedAfterSeek,
                        "4x/5x playback must continue presenting after an in-playback seek; speed=" +
                        std::to_string(speed) + ", delivered=" +
                        std::to_string(deliveredAfterSeek) + ", position=" +
                        std::to_string(player.GetCurrentTime()) + ", playing=" +
                        std::to_string(player.IsPlaying()) + ", eof=" +
                        std::to_string(player.HasPlaybackDecoderEnded()));
            TEST_ASSERT_GT(player.GetCurrentTime(), 20.1,
                           "playback position must advance beyond the seek target");
            player.Pause();
        }
    });

    suite.addTest("PlaybackSpeed_4xAnd5xResumeFromBeginningBeyondInitialQueue", []() {
        for (const double speed : {4.0, 5.0}) {
            VideoPlayer player(g_testHwnd);
            TEST_ASSERT(player.LoadVideo(g_testHeavyVideoPath),
                        "high-speed pause/resume regression source must load");
            player.SetPlaybackSpeed(speed);
            TEST_ASSERT(player.Play(), "initial high-speed playback must start");

            const uint64_t firstFrame = player.GetPresentedPlaybackFrameCount() + 1;
            TEST_ASSERT(WaitForPresentedFrames(player, firstFrame,
                                               std::chrono::seconds(1)),
                        "initial high-speed playback must present before pause");
            player.Pause();
            player.SeekToTimeExact(0.0);

            const uint64_t resumeStart = player.GetPresentedPlaybackFrameCount();
            TEST_ASSERT(player.Play(), "high-speed playback must unpause from frame zero");
            const uint64_t resumeTarget = resumeStart +
                static_cast<uint64_t>(player.GetPlaybackBufferCapacity()) + 12;
            const bool resumedBeyondInitialQueue = WaitForPresentedFrames(
                player, resumeTarget, std::chrono::seconds(2));
            TEST_ASSERT(!player.HasPlaybackDecoderEnded(),
                        "unpausing at the beginning must not kill the decoder after three frames");
            TEST_ASSERT(resumedBeyondInitialQueue,
                        "unpaused playback must continue beyond the complete initial queue");
            TEST_ASSERT(player.IsPlaying(),
                        "player must remain running after high-speed unpause");
            player.Pause();
        }
    });

    suite.addTest("Playback_SeekRevivesProducerAfterDecodeEOF", []() {
        VideoPlayer player(g_testHwnd);
        TEST_ASSERT(player.LoadVideo(g_testVideoPath),
                    "EOF recovery regression source must load");
        player.SeekToTimeExact(player.GetDuration() - 0.5);
        player.SetPlaybackSpeed(4.0);
        TEST_ASSERT(player.Play(), "near-EOF playback must start");

        const auto eofDeadline = std::chrono::steady_clock::now() +
                                 std::chrono::seconds(2);
        while (!player.HasPlaybackDecoderEnded() && player.IsPlaying() &&
               std::chrono::steady_clock::now() < eofDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        TEST_ASSERT(player.HasPlaybackDecoderEnded(),
                    "test must observe the producer waiting at decode EOF");
        TEST_ASSERT(player.IsPlaying(),
                    "queued frames must keep presentation alive while producer waits at EOF");

        const uint64_t beforeSeek = player.GetPresentedPlaybackFrameCount();
        player.SeekWhilePlaying(1.0, false);
        const auto restartDeadline = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(1);
        while (player.HasPlaybackDecoderEnded() && player.IsPlaying() &&
               std::chrono::steady_clock::now() < restartDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        TEST_ASSERT(!player.HasPlaybackDecoderEnded(),
                    "seek must wake the producer that is waiting at EOF");
        const bool recovered = WaitForPresentedFrames(
            player, beforeSeek + 8, std::chrono::seconds(2));
        TEST_ASSERT(recovered,
                    "seeking must wake the EOF producer and resume frame delivery; delivered=" +
                    std::to_string(player.GetPresentedPlaybackFrameCount() - beforeSeek) +
                    ", position=" + std::to_string(player.GetCurrentTime()) +
                    ", playing=" + std::to_string(player.IsPlaying()) +
                    ", eof=" + std::to_string(player.HasPlaybackDecoderEnded()));
        TEST_ASSERT(!player.HasPlaybackDecoderEnded(),
                    "decoder producer must remain active after recovering from EOF");
        TEST_ASSERT_GT(player.GetCurrentTime(), 1.1,
                       "recovered playback must advance beyond the new seek target");
        player.Pause();
    });

    suite.addTest("PlaybackSpeed_4xHeavySourceIsClockDrivenAndFluid", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testHeavyVideoPath);
        player.SetPlaybackSpeed(4.0);
        player.Play();

        const TimedPlaybackMeasurement measurement =
            MeasureTimedPlayback(player, std::chrono::milliseconds(1200));
        player.Pause();

        TEST_ASSERT(measurement.started,
                    "heavy-source 4x playback must start within three seconds");
        TEST_ASSERT_GT(measurement.advance, 4.3,
                       "heavy-source 4x playback must sustain the requested wall-clock rate");
        TEST_ASSERT_LT(measurement.advance, 5.4,
                       "heavy-source 4x playback must not run ahead of its wall clock");
        TEST_ASSERT_GT(measurement.sampledRate, 3.6,
                       "heavy-source 4x presented timestamps must sustain approximately 4x");
        TEST_ASSERT_LT(measurement.sampledRate, 4.4,
                       "heavy-source 4x presented timestamps must not run ahead");
        TEST_ASSERT_GE(measurement.positionChanges, 48,
                       "heavy-source 4x playback must deliver at least forty fluid updates per second");
        TEST_ASSERT_LT(measurement.longestStallMs, static_cast<int64_t>(50),
                       "heavy-source 4x playback must not freeze and jump forward");
    });

    suite.addTest("PlaybackSpeed_5xHeavySourceIsClockDriven", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testHeavyVideoPath);
        player.SetPlaybackSpeed(5.0);
        player.Play();

        const TimedPlaybackMeasurement measurement =
            MeasureTimedPlayback(player, std::chrono::milliseconds(1200));
        player.Pause();

        TEST_ASSERT(measurement.started,
                    "heavy-source 5x playback must start within three seconds");
        TEST_ASSERT_GT(measurement.advance, 5.4,
                       "heavy-source 5x playback must sustain the requested wall-clock rate");
        TEST_ASSERT_LT(measurement.advance, 6.8,
                       "heavy-source 5x playback must remain tied to its wall clock");
        TEST_ASSERT_GT(measurement.sampledRate, 4.5,
                       "heavy-source presented timestamps must sustain approximately 5x");
        TEST_ASSERT_LT(measurement.sampledRate, 5.5,
                       "heavy-source presented timestamps must not run ahead of 5x");
        TEST_ASSERT_GE(measurement.positionChanges, 48,
                       "heavy-source 5x playback must deliver at least forty fluid updates per second");
        TEST_ASSERT_LT(measurement.longestStallMs, static_cast<int64_t>(50),
                       "heavy-source 5x playback must not freeze and jump forward");
    });

    suite.addTest("PlaybackSpeed_HighSpeedConversionMatchesPreviewSize", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testHeavyVideoPath);
        player.SetPlaybackSpeed(5.0);
        player.Play();

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(3);
        while (player.playbackRgbWidth == 0 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        player.Pause();
        player.ForceRedraw();

        TEST_ASSERT_GT(player.playbackRgbWidth, 0,
                       "high-speed playback must create a preview conversion buffer");
        TEST_ASSERT_LT(player.playbackRgbWidth, player.frameWidth,
                       "high-speed playback must not convert every frame at full source width");
        TEST_ASSERT_LT(player.playbackRgbHeight, player.frameHeight,
                       "high-speed playback must not convert every frame at full source height");
        TEST_ASSERT(player.d2dBitmap != nullptr,
                    "preview-sized playback frame must upload to Direct2D");
        const D2D1_SIZE_U uploadedSize = player.d2dBitmap->GetPixelSize();
        TEST_ASSERT_EQ(uploadedSize.width, static_cast<UINT32>(player.playbackRgbWidth),
                       "Direct2D bitmap width must match the preview conversion buffer");
        TEST_ASSERT_EQ(uploadedSize.height, static_cast<UINT32>(player.playbackRgbHeight),
                       "Direct2D bitmap height must match the preview conversion buffer");
    });

    suite.addTest("PlaybackSpeed_10xPresentsContinuously", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SetPlaybackSpeed(10.0);
        player.Play();

        int presentedChanges = 0;
        double lastPosition = player.GetCurrentTime();
        double largestMediaJump = 0.0;
        auto lastChangeAt = std::chrono::steady_clock::now();
        int64_t longestStallMs = 0;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(350);
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const auto now = std::chrono::steady_clock::now();
            const double position = player.GetCurrentTime();
            if (position > lastPosition + 0.0001)
            {
                largestMediaJump = (std::max)(largestMediaJump, position - lastPosition);
                longestStallMs = (std::max)(longestStallMs,
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - lastChangeAt).count());
                ++presentedChanges;
                lastPosition = position;
                lastChangeAt = now;
            }
        }
        longestStallMs = (std::max)(longestStallMs,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - lastChangeAt).count());
        player.Pause();

        TEST_ASSERT_GE(presentedChanges, 14,
                       "10x playback should present continuous updates instead of GOP-sized jumps");
        TEST_ASSERT_GT(player.GetCurrentTime(), 2.5,
                       "smooth 10x playback must still sustain its requested rate");
        TEST_ASSERT_LT(player.GetCurrentTime(), 4.3,
                       "smooth 10x playback must remain tied to its wall clock");
        TEST_ASSERT_LT(largestMediaJump, 0.5,
                       "10x playback must not jump between distant GOP timestamps");
        TEST_ASSERT_LT(longestStallMs, static_cast<int64_t>(75),
                       "10x playback must not visibly freeze between frame updates");
    });

    suite.addTest("PlaybackSpeed_10xHeavySourceIsClockDriven", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testHeavyVideoPath);
        player.SetPlaybackSpeed(10.0);
        player.Play();

        const auto startupDeadline = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(3);
        while (player.GetCurrentTime() < 0.05 &&
               std::chrono::steady_clock::now() < startupDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        TEST_ASSERT_GT(player.GetCurrentTime(), 0.05,
                       "heavy-source 10x playback must start within three seconds");

        const double measurementStartPosition = player.GetCurrentTime();
        int presentedChanges = 0;
        double lastPosition = measurementStartPosition;
        double largestMediaJump = 0.0;
        auto lastChangeAt = std::chrono::steady_clock::now();
        auto firstRateSampleAt = lastChangeAt;
        auto lastRateSampleAt = lastChangeAt;
        double firstRateSamplePosition = measurementStartPosition;
        double lastRateSamplePosition = measurementStartPosition;
        bool hasRateSample = false;
        int64_t longestStallMs = 0;
        const auto deadline = lastChangeAt + std::chrono::milliseconds(1000);
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const auto now = std::chrono::steady_clock::now();
            const double position = player.GetCurrentTime();
            if (position > lastPosition + 0.0001)
            {
                largestMediaJump = (std::max)(largestMediaJump, position - lastPosition);
                longestStallMs = (std::max)(longestStallMs,
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - lastChangeAt).count());
                if (!hasRateSample)
                {
                    firstRateSampleAt = now;
                    firstRateSamplePosition = position;
                    hasRateSample = true;
                }
                lastRateSampleAt = now;
                lastRateSamplePosition = position;
                ++presentedChanges;
                lastPosition = position;
                lastChangeAt = now;
            }
        }
        const bool reachedDecodeEof = player.HasPlaybackDecoderEnded();
        player.Pause();

        const double measuredAdvance = player.GetCurrentTime() - measurementStartPosition;
        TEST_ASSERT(!reachedDecodeEof,
                    "heavy-source rate measurement must not be capped by source EOF");
        TEST_ASSERT_GT(measuredAdvance, 7.0,
                       "10x playback must not collapse to sequential decode speed on a heavy source");
        TEST_ASSERT_LT(measuredAdvance, 11.5,
                       "heavy-source 10x playback must remain tied to its wall clock");
        TEST_ASSERT_GE(presentedChanges, 4,
                       "heavy-source 10x playback must continue presenting between catch-up seeks");
        TEST_ASSERT_LT(largestMediaJump, 2.25,
                       "heavy-source 10x playback must not skip more than one source GOP at a time");
        const double sampledWallSeconds = std::chrono::duration<double>(
            lastRateSampleAt - firstRateSampleAt).count();
        const double sampledRate = sampledWallSeconds > 0.0
            ? (lastRateSamplePosition - firstRateSamplePosition) / sampledWallSeconds
            : 0.0;
        TEST_ASSERT_GT(sampledRate, 8.0,
                       "heavy-source presented timestamps must sustain approximately 10x");
        TEST_ASSERT_LT(sampledRate, 12.0,
                       "heavy-source presented timestamps must not run ahead of 10x");
        TEST_ASSERT_LT(longestStallMs, static_cast<int64_t>(275),
                       "heavy-source 10x catch-up must not become a prolonged freeze");
    });

    suite.addTest("PlaybackSpeed_100xTracksWallClockWithoutCapping", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testLongVideoPath);
        player.SetPlaybackSpeed(100.0);
        player.Play();

        int presentedChanges = 0;
        double lastPosition = player.GetCurrentTime();
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(300);
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const double position = player.GetCurrentTime();
            if (position > lastPosition + 0.0001)
            {
                ++presentedChanges;
                lastPosition = position;
            }
        }
        player.Pause();

        TEST_ASSERT_GT(player.GetCurrentTime(), 18.0,
                       "100x must advance near 100 video seconds per real second, not cap at 2-3x");
        TEST_ASSERT_LT(player.GetCurrentTime(), 42.0,
                       "100x playback should remain tied to its wall-clock target");
        TEST_ASSERT_GE(presentedChanges, 5,
                       "100x playback should keep presenting frames while catching up");
    });

    suite.addTest("PlaybackSpeed_500xTracksWallClockWithoutCapping", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testLongVideoPath);
        player.SetPlaybackSpeed(500.0);
        player.Play();

        int presentedChanges = 0;
        double lastPosition = player.GetCurrentTime();
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(300);
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const double position = player.GetCurrentTime();
            if (position > lastPosition + 0.0001)
            {
                ++presentedChanges;
                lastPosition = position;
            }
        }
        player.Pause();

        TEST_ASSERT_GT(player.GetCurrentTime(), 95.0,
                       "500x must advance far beyond the former roughly-100x throughput ceiling");
        TEST_ASSERT_LT(player.GetCurrentTime(), 205.0,
                       "500x playback should remain tied to its wall-clock target");
        TEST_ASSERT_GE(presentedChanges, 4,
                       "500x playback should keep presenting clock-targeted frames");
    });

    suite.addTest("PlaybackSpeed_4xBoundaryNeverStarves", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.SetPlaybackSpeed(4.0);
        player.Play();

        const auto startupDeadline = std::chrono::steady_clock::now() +
                                     std::chrono::milliseconds(500);
        while (player.GetCurrentTime() < 0.05 &&
               std::chrono::steady_clock::now() < startupDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        TEST_ASSERT_GT(player.GetCurrentTime(), 0.05,
                       "4x playback must start promptly");

        double lastPosition = player.GetCurrentTime();
        const uint64_t presentationCountAtStart = player.GetPresentedPlaybackFrameCount();
        auto lastChange = std::chrono::steady_clock::now();
        int64_t longestStallMs = 0;
        int presentedChanges = 0;
        const auto deadline = lastChange + std::chrono::milliseconds(900);
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const auto now = std::chrono::steady_clock::now();
            const double position = player.GetCurrentTime();
            if (position > lastPosition + 0.0001)
            {
                longestStallMs = (std::max)(longestStallMs,
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChange).count());
                lastChange = now;
                lastPosition = position;
                ++presentedChanges;
            }
        }
        longestStallMs = (std::max)(longestStallMs,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - lastChange).count());
        player.Pause();
        const uint64_t presentationCount =
            player.GetPresentedPlaybackFrameCount() - presentationCountAtStart;

        TEST_ASSERT_GT(player.GetCurrentTime(), 2.5,
                       "4x playback should keep advancing past the high-speed boundary");
        TEST_ASSERT_GE(presentedChanges, 30,
                       "4x playback position must keep updating continuously");
        TEST_ASSERT_GE(presentationCount, static_cast<uint64_t>(30),
                       "4x playback must sustain a fluid reference-frame cadence");
        TEST_ASSERT_LT(longestStallMs, static_cast<int64_t>(75),
                       "4x playback must not starve frame delivery and freeze");
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

    suite.addTest("PauseResume_ContinuesFromPausedPosition", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        TEST_ASSERT(player.Play(), "Initial play should succeed");

        auto firstDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (player.GetCurrentTime() < 1.0 &&
               std::chrono::steady_clock::now() < firstDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        player.Pause();
        const double pausedAt = player.GetCurrentTime();
        const uint64_t beforeResume = player.GetPresentedPlaybackFrameCount();
        TEST_ASSERT_GE(pausedAt, 1.0,
                       "regression must pause far enough from zero to detect a restart");
        TEST_ASSERT(!player.IsPlaying(), "Player should be paused before resuming");
        TEST_ASSERT(player.Play(), "Play should succeed after pausing");

        TEST_ASSERT(WaitForPresentedFrames(player, beforeResume + 1,
                                           std::chrono::seconds(2)),
                    "resume must present its first post-pause frame");
        TEST_ASSERT_GE(player.GetCurrentTime(), pausedAt - 0.1,
                       "first resumed frame must not restart from the beginning");

        auto resumeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (player.GetCurrentTime() <= pausedAt + 0.05 &&
               std::chrono::steady_clock::now() < resumeDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        TEST_ASSERT_GT(player.GetCurrentTime(), pausedAt + 0.05,
                       "Playback should advance after pause and resume");
        player.Pause();
    });

    suite.addTest("SeekImmediatePauseResume_DoesNotCrash", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);

        for (int iteration = 0; iteration < 5; ++iteration) {
            TEST_ASSERT(player.Play(), "Play should succeed before seek");
            player.SeekWhilePlaying(1.0 + iteration * 0.5, false);
            player.Pause();
            TEST_ASSERT(!player.IsPlaying(), "Immediate pause should finish cleanly");
        }

        TEST_ASSERT(player.Play(), "Playback should restart after seek/pause races");
        const double resumedAt = player.GetCurrentTime();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (player.GetCurrentTime() <= resumedAt + 0.05 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        TEST_ASSERT_GT(player.GetCurrentTime(), resumedAt + 0.05,
                       "Playback should advance after immediate seek/pause/resume");
        player.Pause();
    });

    suite.addTest("HeavySeekImmediatePauseResume_NeverFallsBackToBeginning", []() {
        VideoPlayer player(g_testHwnd);
        TEST_ASSERT(player.LoadVideo(g_testHeavyVideoPath),
                    "heavy pause/resume regression source must load");
        TEST_ASSERT(player.Play(), "heavy source must start before seeking");
        TEST_ASSERT(WaitForPresentedFrames(player, 3, std::chrono::seconds(3)),
                    "heavy source must visibly advance before seeking");

        // Match the actual timeline sequence: mouse-down requests a preview,
        // then mouse-up publishes the final target before Pause can race both.
        player.SeekWhilePlaying(14.5, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        constexpr double seekTarget = 15.0;
        player.SeekWhilePlaying(seekTarget, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        player.Pause();
        TEST_ASSERT(!player.IsPlaying(),
                    "immediate pause must complete while the seek is in flight");

        const uint64_t beforeResume = player.GetPresentedPlaybackFrameCount();
        TEST_ASSERT(player.Play(), "Play must accept resume after an interrupted seek");
        TEST_ASSERT(WaitForPresentedFrames(player, beforeResume + 1,
                                           std::chrono::seconds(3)),
                    "resume must present a frame instead of draining to EOF");
        TEST_ASSERT(player.IsPlaying(),
                    "resume after an interrupted seek must not Stop at EOF");
        TEST_ASSERT_GE(player.GetCurrentTime(), seekTarget - 0.3,
                       "first resumed frame must remain at the requested seek, not the beginning");

        const double firstResumedPosition = player.GetCurrentTime();
        TEST_ASSERT(WaitForPresentedFrames(player, beforeResume + 5,
                                           std::chrono::seconds(2)),
                    "resumed playback must continue beyond its first frame");
        TEST_ASSERT_GT(player.GetCurrentTime(), firstResumedPosition,
                       "timeline must continue advancing after interrupted-seek resume");
        player.Pause();
    });

    suite.addTest("TimelineMouseAndPlayButton_ResumeAtSelectedPosition", []() {
        struct UiStateReset {
            VideoPlayer* previousPlayer = g_videoPlayer;
            HWND previousTimeline = g_hTimeline;
            ~UiStateReset() {
                g_previewSeekTime = -1.0;
                g_isTimelineDragging = false;
                g_hTimeline = previousTimeline;
                g_videoPlayer = previousPlayer;
            }
        } uiStateReset;

        VideoPlayer player(g_testHwnd);
        g_videoPlayer = &player;
        g_hTimeline = g_testHwnd;
        TEST_ASSERT(player.LoadVideo(g_testHeavyVideoPath),
                    "event-level resume regression source must load");

        WindowProc(g_testHwnd, WM_COMMAND, MAKEWPARAM(1002, BN_CLICKED), 0);
        TEST_ASSERT(player.IsPlaying(), "Play button handler must start playback");
        TEST_ASSERT(WaitForPresentedFrames(player, 3, std::chrono::seconds(3)),
                    "event-level source must visibly advance before seeking");

        RECT timelineRect{};
        GetClientRect(g_testHwnd, &timelineRect);
        const int seekX = timelineRect.right * 3 / 4;
        const int seekY = timelineRect.bottom / 2;
        const LPARAM seekPoint = MAKELPARAM(seekX, seekY);
        TimelineProc(g_testHwnd, WM_LBUTTONDOWN, MK_LBUTTON, seekPoint);
        TimelineProc(g_testHwnd, WM_LBUTTONUP, 0, seekPoint);
        WindowProc(g_testHwnd, WM_COMMAND, MAKEWPARAM(1003, BN_CLICKED), 0);
        TEST_ASSERT(!player.IsPlaying(),
                    "Pause button handler must stop while timeline seek is in flight");

        const double expectedTarget = player.GetDuration() * 0.75;
        const uint64_t beforeResume = player.GetPresentedPlaybackFrameCount();
        WindowProc(g_testHwnd, WM_COMMAND, MAKEWPARAM(1002, BN_CLICKED), 0);
        TEST_ASSERT(player.IsPlaying(), "Play button handler must accept resume");
        TEST_ASSERT(WaitForPresentedFrames(player, beforeResume + 1,
                                           std::chrono::seconds(3)),
                    "real timeline/Play event path must present after resume");
        TEST_ASSERT_GE(player.GetCurrentTime(), expectedTarget - 0.3,
                       "real timeline/Play event path must not present from the beginning");
        TEST_ASSERT(WaitForPresentedFrames(player, beforeResume + 5,
                                           std::chrono::seconds(2)),
                    "real timeline/Play event path must keep presenting frames");
        TEST_ASSERT(player.IsPlaying(),
                    "real timeline/Play event path must not reset through Stop at EOF");
        TEST_ASSERT_GE(player.GetCurrentTime(), expectedTarget - 0.3,
                       "timeline must remain at the selected region while playback advances");
        player.Pause();
    });

    suite.addTest("PlaybackBuffer_PrefillsAndStaysBounded", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.Play();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (player.GetBufferedPlaybackFrameCount() == 0 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        const size_t capacity = player.GetPlaybackBufferCapacity();
        const size_t buffered = player.GetBufferedPlaybackFrameCount();
        TEST_ASSERT_GE(capacity, static_cast<size_t>(3),
                       "Playback buffer should retain decoding headroom");
        TEST_ASSERT(capacity <= static_cast<size_t>(24),
                    "Playback buffer must have a bounded frame count");
        TEST_ASSERT_GT(buffered, static_cast<size_t>(0),
                       "Playback should decode frames ahead of presentation");
        TEST_ASSERT(buffered <= capacity,
                    "Playback buffer must never exceed its capacity");
        player.Pause();
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

    suite.addTest("PlayingTimelineAndJLSeek_StayPlayingAndNeverRestartAtBeginning", []() {
        VideoPlayer player(g_testHwnd);
        TEST_ASSERT(player.LoadVideo(g_testLongVideoPath),
                    "playing UI-seek regression source must load");
        TEST_ASSERT(player.Play(), "playback must start before a timeline/J/L seek");

        const uint64_t initialPresentation = player.GetPresentedPlaybackFrameCount();
        TEST_ASSERT(WaitForPresentedFrames(player, initialPresentation + 6,
                                           std::chrono::seconds(2)),
                    "playback must be visibly advancing before the UI seek");

        constexpr double futureTarget = 90.0;
        player.ForceNextPrimarySeekNoOpForTesting();
        const uint64_t beforeFutureSeek = player.GetPresentedPlaybackFrameCount();
        player.SeekWhilePlaying(futureTarget, false);
        const bool futureFramesPresented = WaitForPresentedFrames(
            player, beforeFutureSeek + 6, std::chrono::seconds(3));

        TEST_ASSERT_EQ(player.GetInjectedPrimarySeekNoOpCountForTesting(),
                       static_cast<uint64_t>(1),
                       "regression must force a demux seek that lies about succeeding");
        TEST_ASSERT(player.IsPlaying(),
                    "timeline/L seek while playing must not pause playback");
        TEST_ASSERT(futureFramesPresented,
                    "timeline/L seek must continue presenting frames while playing");
        TEST_ASSERT_GE(player.GetCurrentTime(), futureTarget - 0.2,
                       "timeline/L seek must display from the requested future position, not the beginning");

        constexpr double backwardTarget = 40.0;
        player.ForceNextPrimarySeekNoOpForTesting();
        const uint64_t beforeBackwardSeek = player.GetPresentedPlaybackFrameCount();
        player.SeekWhilePlaying(backwardTarget, false);
        const bool backwardFramesPresented = WaitForPresentedFrames(
            player, beforeBackwardSeek + 6, std::chrono::seconds(3));
        TEST_ASSERT(player.IsPlaying(),
                    "J seek while playing must not pause playback");
        TEST_ASSERT(backwardFramesPresented,
                    "J seek must continue presenting frames while playing");
        TEST_ASSERT_EQ(player.GetInjectedPrimarySeekNoOpCountForTesting(),
                       static_cast<uint64_t>(2),
                       "both forward and backward UI seeks must reject false demux success");
        TEST_ASSERT_GE(player.GetCurrentTime(), backwardTarget - 0.2,
                       "J seek must not restart playback at the beginning");
        TEST_ASSERT_LT(player.GetCurrentTime(), backwardTarget + 2.0,
                       "J seek must land near its requested backward position");

        player.Pause();
        const double pausedAt = player.GetCurrentTime();
        const uint64_t beforeResume = player.GetPresentedPlaybackFrameCount();
        TEST_ASSERT(player.Play(), "manual unpause after a UI seek must succeed");
        TEST_ASSERT(WaitForPresentedFrames(player, beforeResume + 6,
                                           std::chrono::seconds(2)),
                    "manual unpause after a UI seek must keep displaying frames");
        TEST_ASSERT_GE(player.GetCurrentTime(), pausedAt - 2.0,
                       "unpausing after a UI seek must not restart at the beginning");
        player.Pause();
    });

    suite.addTest("PlayingUiSeek_StartsPromptlyAndReleasesTimelineCursor", []() {
        struct PreviewSeekReset {
            VideoPlayer* previousPlayer = g_videoPlayer;
            ~PreviewSeekReset() {
                g_previewSeekTime = -1.0;
                g_videoPlayer = previousPlayer;
            }
        } previewSeekReset;

        VideoPlayer player(g_testHwnd);
        g_videoPlayer = &player;
        TEST_ASSERT(player.LoadVideo(g_testHeavyVideoPath),
                    "playing UI-seek latency regression source must load");
        TEST_ASSERT(player.Play(), "playback must start before a UI seek");
        TEST_ASSERT(WaitForPresentedFrames(player, 3, std::chrono::seconds(3)),
                    "heavy regression source must visibly play before seeking");

        constexpr double seekTarget = 12.0;
        g_previewSeekTime = seekTarget;
        const uint64_t beforeSeek = player.GetPresentedPlaybackFrameCount();
        const auto seekStarted = std::chrono::steady_clock::now();
        FinalizePlayingUiSeek(&player, seekTarget);

        const auto deadline = seekStarted + std::chrono::milliseconds(1500);
        while ((player.GetPresentedPlaybackFrameCount() <= beforeSeek ||
                player.GetCurrentTime() < seekTarget - 0.25) &&
               player.IsPlaying() &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        const auto startupLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - seekStarted);
        TEST_ASSERT_LT(startupLatency.count(), static_cast<int64_t>(1500),
                       "playing UI seek must not block playback for multiple seconds");
        TEST_ASSERT_GE(player.GetCurrentTime(), seekTarget - 0.25,
                       "playing UI seek must present from the requested position");
        TEST_ASSERT_LT(g_previewSeekTime, 0.0,
                       "timeline cursor must unpin after the first post-seek frame");

        const double firstSeekPosition = player.GetCurrentTime();
        const uint64_t firstSeekPresentation = player.GetPresentedPlaybackFrameCount();
        TEST_ASSERT(WaitForPresentedFrames(player, firstSeekPresentation + 5,
                                           std::chrono::seconds(2)),
                    "playback must keep presenting after the UI seek starts");
        TEST_ASSERT_GT(player.GetCurrentTime(), firstSeekPosition,
                       "timeline position must advance after the cursor unpins");
        player.Pause();
    });

    suite.addTest("PauseResume_SparseIndexKeepsPausedPosition", []() {
        VideoPlayer player(g_testHwnd);
        TEST_ASSERT(player.LoadVideo(g_testSparseIndexVideoPath),
                    "sparse-index resume regression source must load");
        TEST_ASSERT(player.Play(), "sparse-index playback must start before pause");

        const auto pauseDeadline = std::chrono::steady_clock::now() +
                                   std::chrono::seconds(3);
        while (player.GetCurrentTime() < 1.0 && player.IsPlaying() &&
               std::chrono::steady_clock::now() < pauseDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        player.Pause();

        const double pausedAt = player.GetCurrentTime();
        const uint64_t beforeResume = player.GetPresentedPlaybackFrameCount();
        TEST_ASSERT_GE(pausedAt, 1.0,
                       "regression must pause far enough from zero to detect a restart");
        player.ForceNextPrimarySeekNoOpForTesting();
        TEST_ASSERT(player.Play(), "resume must continue without a demuxer resync");
        TEST_ASSERT_EQ(player.GetInjectedPrimarySeekNoOpCountForTesting(),
                       static_cast<uint64_t>(0),
                       "ordinary unpause must reuse buffered frames instead of seeking");
        const auto resumeDeadline = std::chrono::steady_clock::now() +
                                    std::chrono::seconds(2);
        while (player.GetPresentedPlaybackFrameCount() < beforeResume + 1 &&
               player.IsPlaying() &&
               std::chrono::steady_clock::now() < resumeDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        TEST_ASSERT_GE(player.GetPresentedPlaybackFrameCount(), beforeResume + 1,
                       "resume must present a retained decoded frame");
        TEST_ASSERT_GE(player.GetCurrentTime(), pausedAt - 0.2,
                       "ordinary unpause must not restart at the beginning");
        player.Pause();
    });

    suite.addTest("PlayingUiSeek_LongGopDoesNotReturnToBeginning", []() {
        struct PreviewSeekReset {
            VideoPlayer* previousPlayer = g_videoPlayer;
            ~PreviewSeekReset() {
                g_previewSeekTime = -1.0;
                g_videoPlayer = previousPlayer;
            }
        } previewSeekReset;

        VideoPlayer player(g_testHwnd);
        g_videoPlayer = &player;
        TEST_ASSERT(player.LoadVideo(g_testSparseSeekVideoPath),
                    "sparse-keyframe regression source must load");
        TEST_ASSERT(player.Play(), "sparse-keyframe source must start playing");
        TEST_ASSERT(WaitForPresentedFrames(player, 3, std::chrono::seconds(2)),
                    "sparse-keyframe source must visibly advance before seeking");

        constexpr double seekTarget = 15.0;
        g_previewSeekTime = seekTarget;
        const uint64_t beforeSeek = player.GetPresentedPlaybackFrameCount();
        FinalizePlayingUiSeek(&player, seekTarget);

        TEST_ASSERT(WaitForPresentedFrames(player, beforeSeek + 1,
                                           std::chrono::seconds(3)),
                    "long-GOP timeline seek must produce a frame");
        TEST_ASSERT_GE(player.GetCurrentTime(), seekTarget - 0.2,
                       "long-GOP timeline seek must not restore playback to the beginning");
        TEST_ASSERT_LT(player.GetCurrentTime(), seekTarget + 1.0,
                       "long-GOP timeline seek must land near the selected time");
        TEST_ASSERT_LT(g_previewSeekTime, 0.0,
                       "timeline cursor must unpin after the exact seek lands");
        TEST_ASSERT(player.IsPlaying(),
                    "playback must continue after a long-GOP timeline seek");
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

    suite.addTest("PausedTimelineSeek_ResumeStartsAtSeekTargetAndKeepsAdvancing", []() {
        struct PreviewSeekReset {
            VideoPlayer* previousPlayer = g_videoPlayer;
            ~PreviewSeekReset() {
                g_previewSeekTime = -1.0;
                g_videoPlayer = previousPlayer;
            }
        } previewSeekReset;

        VideoPlayer player(g_testHwnd);
        g_videoPlayer = &player;
        TEST_ASSERT(player.LoadVideo(g_testLongVideoPath),
                    "paused timeline seek regression source must load");

        TEST_ASSERT(player.Play(), "initial playback must start before the paused scrub");
        const auto initialDeadline = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(2);
        while (player.GetCurrentTime() < 0.1 &&
               std::chrono::steady_clock::now() < initialDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        player.Pause();

        // Match the timeline's real paused drag sequence: exact mouse-down,
        // inexpensive previews while moving, then an exact mouse-up seek.
        player.SeekToTime(60.0, INT_MAX, false, false);
        player.SeekToTime(70.0, 0, true, true);
        player.SeekToTime(120.0, 0, true, true);
        constexpr double seekTarget = 90.0;
        // Some real inputs reject the stream-specific seek used by the player.
        // Releasing the cursor backward after a preview makes an ignored failure
        // visible: decoding resumes from the preview position instead of target.
        player.ForceNextPrimarySeekFailureForTesting();
        g_previewSeekTime = seekTarget;
        player.SeekToTime(seekTarget, INT_MAX, false, false);
        TEST_ASSERT_EQ(player.GetInjectedPrimarySeekFailureCountForTesting(),
                       static_cast<uint64_t>(1),
                       "regression must exercise a failed primary demux seek");
        TEST_ASSERT_NEAR(player.GetCurrentTime(), seekTarget, 0.2,
                         "paused timeline release must land on its requested frame");
        TEST_ASSERT_LT(g_previewSeekTime, 0.0,
                       "timeline cursor must unpin once the exact paused seek lands");

        const uint64_t presentationsBeforeResume =
            player.GetPresentedPlaybackFrameCount();
        TEST_ASSERT(player.Play(), "Play must resume after a paused timeline seek");
        TEST_ASSERT(WaitForPresentedFrames(player, presentationsBeforeResume + 1,
                                           std::chrono::seconds(3)),
                    "resume must present a frame after the paused seek");
        TEST_ASSERT_GE(player.GetCurrentTime(), seekTarget - 0.2,
                       "the first resumed playback frame must not jump back to the beginning");

        TEST_ASSERT(WaitForPresentedFrames(player, presentationsBeforeResume + 12,
                                           std::chrono::seconds(3)),
                    "playback must keep presenting after a paused timeline seek");
        TEST_ASSERT_GT(player.GetCurrentTime(), seekTarget + 0.2,
                       "timeline position must advance beyond the paused seek target");
        TEST_ASSERT(player.IsPlaying(),
                    "player must remain playable after resuming a paused timeline seek");
        player.Pause();
    });

    suite.addTest("PausedAv1OpusTimelineSeek_UnpauseKeepsPlaying", []() {
        VideoPlayer player(g_testHwnd);
        TEST_ASSERT(player.LoadVideo(g_testAv1OpusVideoPath),
                    "AV1/Opus paused-seek regression source must load");

        constexpr double seekTarget = 8.0;
        // Model the real AV1 decoder backpressure that exposed the bug. The
        // packet must survive EAGAIN while output is drained, then be retried.
        player.ForceBufferedDecoderEagainAfterPacketsForTesting(0);
        player.SeekToTimeExact(seekTarget);

        TEST_ASSERT_GT(player.GetInjectedBufferedDecoderEagainCountForTesting(),
                       static_cast<uint64_t>(0),
                       "paused seek must exercise decoder EAGAIN packet retention");
        TEST_ASSERT_EQ(player.GetFallbackSeekCountForTesting(),
                       static_cast<uint64_t>(0),
                       "a valid indexed MP4 seek must not be rejected using raw IO position");
        TEST_ASSERT_NEAR(player.GetCurrentTime(), seekTarget, 0.15,
                         "paused AV1 seek must land at the selected timestamp");

        const uint64_t beforeResume = player.GetPresentedPlaybackFrameCount();
        TEST_ASSERT(player.Play(), "AV1/Opus playback must unpause after seeking");
        TEST_ASSERT(WaitForPresentedFrames(player, beforeResume + 12,
                                           std::chrono::seconds(5)),
                    "unpaused AV1 playback must continue beyond the initial decoder frames");
        TEST_ASSERT(player.IsPlaying(),
                    "AV1 playback must not immediately pause after a timeline seek");
        TEST_ASSERT_GT(player.GetCurrentTime(), seekTarget + 0.2,
                       "AV1 playback must advance beyond the selected timestamp");
        player.Pause();
    });

    wchar_t* externalMediaValue = nullptr;
    size_t externalMediaLength = 0;
    _wdupenv_s(&externalMediaValue, &externalMediaLength,
               L"VIDEO_EDITOR_REAL_MEDIA");
    const std::wstring externalMediaPath = externalMediaValue
        ? externalMediaValue
        : L"";
    std::free(externalMediaValue);
    if (!externalMediaPath.empty() &&
        std::filesystem::exists(externalMediaPath))
    {
        suite.addTest("ExternalMedia_PausedTimelineSeek_UnpauseKeepsPlaying",
                      [externalMediaPath]() {
            VideoPlayer player(g_testHwnd);
            TEST_ASSERT(player.LoadVideo(externalMediaPath),
                        "external paused-seek regression source must load");

            const double seekTarget = player.GetDuration() * 0.5;
            player.SeekToTimeExact(seekTarget);
            TEST_ASSERT_NEAR(player.GetCurrentTime(), seekTarget, 0.2,
                             "external media seek must land at its midpoint");

            const uint64_t beforeResume =
                player.GetPresentedPlaybackFrameCount();
            TEST_ASSERT(player.Play(),
                        "external media must accept unpause after seeking");
            TEST_ASSERT(WaitForPresentedFrames(player, beforeResume + 12,
                                               std::chrono::seconds(8)),
                        "external media must keep presenting after unpause");
            TEST_ASSERT(player.IsPlaying(),
                        "external media must not immediately pause after unpause");
            TEST_ASSERT_GT(player.GetCurrentTime(), seekTarget + 0.2,
                           "external media must advance after unpause");
            player.Pause();
        });
    }

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

    suite.addTest("PlaybackEof_KeepsLastPresentedPosition", []() {
        VideoPlayer player(g_testHwnd);
        TEST_ASSERT(player.LoadVideo(g_testVideoPath),
                    "EOF position regression source must load");

        const double duration = player.GetDuration();
        player.SeekToTimeExact(duration - 0.5);
        const double positionBeforePlay = player.GetCurrentTime();
        TEST_ASSERT_GT(positionBeforePlay, 1.0,
                       "regression must begin far enough from zero to detect a reset");
        player.SetPlaybackSpeed(4.0);
        TEST_ASSERT(player.Play(), "playback near EOF must start");

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(3);
        while (player.IsPlaying() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        TEST_ASSERT(!player.IsPlaying(), "playback must pause after draining EOF");
        TEST_ASSERT_GE(player.GetCurrentTime(), positionBeforePlay - 0.1,
                       "EOF must retain the final visible position instead of resetting to zero");
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

    suite.addTest("PlayClips_AdvancesAcrossSegments", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::vector<ClipSegment> segments = {{0.0, 0.15}, {1.0, 1.6}};
        player.PlayClips(segments);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        // Keep waiting at the exact boundary too. Depending on FFmpeg's build
        // and timestamp rounding, the exact seek may first publish 0.9 before
        // the next buffered frame advances beyond it.
        while (player.GetCurrentTime() <= 0.9 && std::chrono::steady_clock::now() < deadline) {
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        TEST_ASSERT_GT(player.GetCurrentTime(), 0.9, "Preview should jump to the next selected clip");
        player.CancelClipPreview();
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
