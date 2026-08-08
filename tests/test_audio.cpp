#include "test_framework.h"
#include "../src/pitch_preserving_stretcher.h"
#include "../src/ten_vad_embedded.h"
#include "../src/video_player.h"
#include "../src/options_window.h"
#include "../src/timeline.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

// ============================================================================
// Integration tests for audio track management
// ============================================================================

extern std::wstring g_testVideoPath;
extern HWND g_testHwnd;

namespace
{
std::size_t CountLegacyTenVadTempDirectories()
{
    wchar_t tempPath[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempPath) == 0)
        return 0;

    const std::wstring pattern =
        std::wstring(tempPath) + L"VideoEditor-ten-vad-*";
    WIN32_FIND_DATAW findData = {};
    HANDLE find = FindFirstFileW(pattern.c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE)
        return 0;

    std::size_t count = 0;
    do
    {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            ++count;
    } while (FindNextFileW(find, &findData));
    FindClose(find);
    return count;
}

bool WaitForCondition(const std::function<bool()>& condition,
                      std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}

void VerifyAudioContinuesAfterPlayingSeek(const std::wstring& mediaPath,
                                          double seekTarget,
                                          std::chrono::milliseconds timeout,
                                          double playbackSpeed = 1.0)
{
    VideoPlayer player(g_testHwnd);
    TEST_ASSERT(player.LoadVideo(mediaPath),
                "audio seek regression source must load");
    player.SetPlaybackSpeed(playbackSpeed);

    // A Windows test runner without an audio endpoint cannot exercise WASAPI.
    // The generated-media and real-media variants run the full check whenever
    // an endpoint is available.
    if (!player.IsAudioOutputAvailableForTesting())
        return;

    TEST_ASSERT(player.Play(), "playback must start for the audio seek test");
    TEST_ASSERT(WaitForCondition([&player]() {
                    return player.GetAudioClientStartCountForTesting() >= 1 &&
                           player.GetSubmittedAudioFrameCountForTesting() > 0;
                }, timeout),
                "audio output must start and submit samples before seeking");

    const uint64_t submittedBeforeSeek =
        player.GetSubmittedAudioFrameCountForTesting();
    const uint64_t startsBeforeSeek =
        player.GetAudioClientStartCountForTesting();
    const uint64_t presentedBeforeSeek =
        player.GetPresentedPlaybackFrameCount();
    const int sampleRate = player.GetAudioSampleRateForTesting();
    const uint64_t continuedAudioFrames = static_cast<uint64_t>(
        sampleRate > 0 ? sampleRate / 4 : 1);

    player.SeekWhilePlaying(seekTarget, true);

    TEST_ASSERT(WaitForCondition([&player, presentedBeforeSeek, startsBeforeSeek,
                                  submittedBeforeSeek, continuedAudioFrames,
                                  seekTarget]() {
                    return player.GetPresentedPlaybackFrameCount() > presentedBeforeSeek &&
                           player.GetAudioClientStartCountForTesting() > startsBeforeSeek &&
                           player.GetSubmittedAudioFrameCountForTesting() >
                               submittedBeforeSeek + continuedAudioFrames &&
                           player.GetCurrentTime() >= seekTarget - 0.25 &&
                           player.GetCurrentTime() <= seekTarget + 2.0;
                }, timeout),
                "audio must restart and submit at least 250 ms after a playing seek");
    TEST_ASSERT_EQ(player.GetAudioClientStartFailureCountForTesting(),
                   static_cast<uint64_t>(0),
                   "WASAPI must never be started twice for the same playback epoch");
    TEST_ASSERT(player.IsPlaying(),
                "an audio restart must not stop video playback after seeking");
    player.Pause();
}

void VerifyAudioContinuesAfterPauseResume(const std::wstring& mediaPath,
                                          std::chrono::milliseconds timeout)
{
    VideoPlayer player(g_testHwnd);
    TEST_ASSERT(player.LoadVideo(mediaPath),
                "audio pause/resume regression source must load");

    if (!player.IsAudioOutputAvailableForTesting())
        return;

    TEST_ASSERT(player.Play(), "playback must start for the pause/resume test");
    TEST_ASSERT(WaitForCondition([&player]() {
                    return player.GetAudioClientStartCountForTesting() >= 1 &&
                           player.GetSubmittedAudioFrameCountForTesting() > 0;
                }, timeout),
                "audio output must start before pausing");

    const int sampleRate = player.GetAudioSampleRateForTesting();
    const uint64_t continuedAudioFrames = static_cast<uint64_t>(
        sampleRate > 0 ? sampleRate / 4 : 1);

    for (int cycle = 0; cycle < 3; ++cycle)
    {
        const uint64_t resetsBeforePause =
            player.GetAudioClientResetCountForTesting();
        player.Pause();
        TEST_ASSERT_GT(player.GetAudioClientResetCountForTesting(),
                       resetsBeforePause,
                       "pause must clear stopped WASAPI padding before resume");

        const uint64_t submittedBeforeResume =
            player.GetSubmittedAudioFrameCountForTesting();
        const uint64_t startsBeforeResume =
            player.GetAudioClientStartCountForTesting();
        const uint64_t presentedBeforeResume =
            player.GetPresentedPlaybackFrameCount();

        TEST_ASSERT(player.Play(), "playback must resume after pause");
        TEST_ASSERT(WaitForCondition(
                        [&player, submittedBeforeResume, startsBeforeResume,
                         presentedBeforeResume, continuedAudioFrames]() {
                            return player.GetAudioClientStartCountForTesting() >
                                       startsBeforeResume &&
                                   player.GetSubmittedAudioFrameCountForTesting() >
                                       submittedBeforeResume + continuedAudioFrames &&
                                   player.GetPresentedPlaybackFrameCount() >
                                       presentedBeforeResume;
                        }, timeout),
                    "audio must restart and keep submitting after pause/resume");
    }

    TEST_ASSERT_EQ(player.GetAudioClientStartFailureCountForTesting(),
                   static_cast<uint64_t>(0),
                   "pause/resume must not double-start the WASAPI client");
    player.Pause();
}

std::vector<float> StretchTestTone(double speed)
{
    constexpr int sampleRate = 48000;
    constexpr int inputFrames = sampleRate * 2;
    constexpr double toneHz = 440.0;
    constexpr double pi = 3.14159265358979323846;
    constexpr size_t blockFrames = 512;

    std::vector<float> input(inputFrames);
    for (int frame = 0; frame < inputFrames; ++frame)
    {
        input[frame] = static_cast<float>(
            0.6 * std::sin(2.0 * pi * toneHz * frame / sampleRate));
    }

    PitchPreservingStretcher stretcher(sampleRate, 1, speed);
    std::vector<float> output;
    std::vector<float> retrieved(4096);
    size_t offset = 0;
    while (offset < input.size())
    {
        const size_t remaining = input.size() - offset;
        const size_t block = remaining < blockFrames ? remaining : blockFrames;
        const bool final = offset + block == input.size();
        stretcher.ProcessInterleaved(input.data() + offset, block, final);
        offset += block;

        while (true)
        {
            const size_t count = stretcher.RetrieveInterleaved(
                retrieved.data(), retrieved.size());
            if (count == 0)
                break;
            output.insert(output.end(), retrieved.begin(),
                          retrieved.begin() + count);
        }
    }
    return output;
}

double EstimateToneFrequency(const std::vector<float>& samples)
{
    constexpr double sampleRate = 48000.0;
    const size_t begin = samples.size() / 4;
    const size_t end = samples.size() * 3 / 4;
    size_t risingCrossings = 0;
    for (size_t index = begin + 1; index < end; ++index)
    {
        if (samples[index - 1] <= 0.0f && samples[index] > 0.0f)
            ++risingCrossings;
    }
    const double measuredSeconds = (end - begin) / sampleRate;
    return measuredSeconds > 0.0
        ? risingCrossings / measuredSeconds
        : 0.0;
}
} // namespace

void RegisterAudioTests(TestSuite& suite) {

    suite.addTest("PlaybackSpeed_HighToOneXRestoresSynchronizedAudio", []() {
        VideoPlayer player(g_testHwnd);
        TEST_ASSERT(player.LoadVideo(g_testVideoPath),
                    "high-to-1x audio regression source must load");
        if (!player.IsAudioOutputAvailableForTesting())
            return;

        player.SetPlaybackSpeed(4.0);
        TEST_ASSERT(player.Play(), "4x playback must start before restoring audio");
        TEST_ASSERT(WaitForCondition([&player]() {
                        return player.GetCurrentTime() >= 0.5 &&
                               player.GetPresentedPlaybackFrameCount() >= 4;
                    }, std::chrono::seconds(2)),
                    "4x playback must advance before returning to 1x");

        const uint64_t startsBefore =
            player.GetAudioClientStartCountForTesting();
        const uint64_t submittedBefore =
            player.GetSubmittedAudioFrameCountForTesting();
        const uint64_t presentationsBefore =
            player.GetPresentedPlaybackFrameCount();
        const int sampleRate = std::max(1, player.GetAudioSampleRateForTesting());

        player.SetPlaybackSpeed(1.0);
        TEST_ASSERT(WaitForCondition(
                        [&player, startsBefore, submittedBefore,
                         presentationsBefore, sampleRate]() {
                            return player.GetAudioClientStartCountForTesting() >
                                       startsBefore &&
                                   player.GetSubmittedAudioFrameCountForTesting() >=
                                       submittedBefore + sampleRate / 4 &&
                                   player.GetPresentedPlaybackFrameCount() >
                                       presentationsBefore;
                        }, std::chrono::milliseconds(2500)),
                    "audio must restart promptly and keep submitting after 4x to 1x");

        const double audioPts = player.GetLastSubmittedAudioPtsForTesting();
        const double videoPts = player.GetCurrentTime();
        TEST_ASSERT_GT(audioPts, 0.0,
                       "the restarted audio epoch must publish its media timestamp");
        TEST_ASSERT_NEAR(audioPts, videoPts, 0.35,
                         "restored audio must stay aligned with the video clock");
        TEST_ASSERT_EQ(player.GetAudioClientStartFailureCountForTesting(),
                       static_cast<uint64_t>(0),
                       "the synchronized restart must not double-start WASAPI");
        player.Pause();
    });

    suite.addTest("PlayingSeek_AudioContinues", []() {
        VerifyAudioContinuesAfterPlayingSeek(
            g_testVideoPath, 3.0, std::chrono::seconds(5));
    });

    suite.addTest("PauseResume_AudioContinues", []() {
        VerifyAudioContinuesAfterPauseResume(
            g_testVideoPath, std::chrono::seconds(5));
    });

    suite.addTest("VariableSpeed_PreservesPitchAndAudioContinues", []() {
        for (const double speed : {0.5, 2.0})
        {
            const std::vector<float> stretched = StretchTestTone(speed);
            const double expectedFrames = 48000.0 * 2.0 / speed;
            TEST_ASSERT_GT(stretched.size(),
                           static_cast<size_t>(expectedFrames * 0.85),
                           "time-stretched tone must have the expected duration");
            TEST_ASSERT_LT(stretched.size(),
                           static_cast<size_t>(expectedFrames * 1.15),
                           "time-stretched tone duration must remain bounded");
            TEST_ASSERT_NEAR(EstimateToneFrequency(stretched), 440.0, 15.0,
                             "playback speed must not change a tone's pitch");

            VerifyAudioContinuesAfterPlayingSeek(
                g_testVideoPath, 3.0, std::chrono::seconds(7), speed);
        }
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
        suite.addTest("ExternalMedia_PlayingSeek_AudioContinues",
                      [externalMediaPath]() {
            double seekTarget = 0.0;
            {
                VideoPlayer probe(g_testHwnd);
                TEST_ASSERT(probe.LoadVideo(externalMediaPath),
                            "external audio seek regression source must load");
                seekTarget = probe.GetDuration() * 0.5;
            }
            VerifyAudioContinuesAfterPlayingSeek(
                externalMediaPath, seekTarget, std::chrono::seconds(10), 2.0);
        });
        suite.addTest("ExternalMedia_PauseResume_AudioContinues",
                      [externalMediaPath]() {
            VerifyAudioContinuesAfterPauseResume(
                externalMediaPath, std::chrono::seconds(15));
        });
    }

    suite.addTest("ExportMasterGain_DbConversion", []() {
        const int savedGainDb = g_exportMasterGainDb;
        g_exportMasterGainDb = 0;
        const float unity = GetExportMasterGainLinear();
        g_exportMasterGainDb = 6;
        const float plusSix = GetExportMasterGainLinear();
        g_exportMasterGainDb = -6;
        const float minusSix = GetExportMasterGainLinear();
        g_exportMasterGainDb = savedGainDb;

        TEST_ASSERT_NEAR(unity, 1.0f, 0.001f, "0 dB should preserve the signal");
        TEST_ASSERT_NEAR(plusSix, 1.995f, 0.002f, "+6 dB should nearly double amplitude");
        TEST_ASSERT_NEAR(minusSix, 0.501f, 0.002f, "-6 dB should nearly halve amplitude");
    });

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

    suite.addTest("EmbeddedTenVad_LoadsAndProcesses", []() {
        const std::size_t tempDirectoryCountBefore =
            CountLegacyTenVadTempDirectories();
        EmbeddedTenVadHandle vad = nullptr;
        TEST_ASSERT(EmbeddedTenVadCreate(&vad, 256, 0.70f),
                    "Embedded TEN VAD resource should load");

        std::array<std::int16_t, 256> silence{};
        float probability = -1.0f;
        int speechFlag = -1;
        TEST_ASSERT(EmbeddedTenVadProcess(
                        vad, silence.data(), silence.size(), &probability,
                        &speechFlag),
                    "Embedded TEN VAD should process a frame");
        TEST_ASSERT(probability >= 0.0f && probability <= 1.0f,
                    "TEN VAD probability should be normalized");
        TEST_ASSERT(speechFlag == 0 || speechFlag == 1,
                    "TEN VAD speech flag should be binary");

        EmbeddedTenVadDestroy(&vad);
        TEST_ASSERT(vad == nullptr,
                     "TEN VAD destroy should clear the handle");
        ShutdownEmbeddedTenVadRuntime();
        TEST_ASSERT_EQ(CountLegacyTenVadTempDirectories(),
                       tempDirectoryCountBefore,
                       "TEN VAD should not extract a directory into %TEMP%");
    });

    suite.addTest("AudioWaveform_ProgressQuery", []() {
        int progress = GetAudioWaveformProgress();
        TEST_ASSERT(progress >= -1 && progress <= 100,
                    "GetAudioWaveformProgress should return valid progress value (-1 to 100)");
    });
}
