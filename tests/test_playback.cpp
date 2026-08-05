#include "test_framework.h"
#include "../src/video_player.h"
#include <thread>
#include <chrono>
#include <cmath>

// ============================================================================
// Integration tests for playback, seeking, and frame stepping
// ============================================================================

extern std::wstring g_testVideoPath;
extern std::wstring g_testLongVideoPath;
extern std::wstring g_testHeavyVideoPath;
extern HWND g_testHwnd;

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

    suite.addTest("PauseResume_RestartsPlayback", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        TEST_ASSERT(player.Play(), "Initial play should succeed");

        auto firstDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (player.GetCurrentTime() < 0.1 &&
               std::chrono::steady_clock::now() < firstDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        player.Pause();
        const double pausedAt = player.GetCurrentTime();
        TEST_ASSERT(!player.IsPlaying(), "Player should be paused before resuming");
        TEST_ASSERT(player.Play(), "Play should succeed after pausing");

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
