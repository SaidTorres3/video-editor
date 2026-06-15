#include "test_framework.h"
#include "../src/video_player.h"
#include "../src/options_window.h"

#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// ============================================================================
// Integration tests for video cutting/exporting + output quality verification
// ============================================================================

extern std::wstring g_testVideoPath;
extern HWND g_testHwnd;
extern std::wstring g_testTempDir;

namespace {

// Helper: open a video file and inspect its streams using FFmpeg API
struct OutputInfo {
    bool valid = false;
    double duration = 0.0;
    int videoStreamCount = 0;
    int audioStreamCount = 0;
    int videoWidth = 0;
    int videoHeight = 0;
    int64_t videoBitrate = 0;
    AVCodecID videoCodecId = AV_CODEC_ID_NONE;
    AVCodecID audioCodecId = AV_CODEC_ID_NONE;
};

OutputInfo InspectOutputFile(const std::wstring& path) {
    OutputInfo info;
    int bufSize = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(bufSize, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &utf8[0], bufSize, nullptr, nullptr);
    utf8.resize(bufSize - 1);

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, utf8.c_str(), nullptr, nullptr) < 0)
        return info;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return info;
    }

    info.valid = true;
    if (fmt->duration != AV_NOPTS_VALUE)
        info.duration = fmt->duration / (double)AV_TIME_BASE;

    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        AVCodecParameters* par = fmt->streams[i]->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            info.videoStreamCount++;
            if (info.videoWidth == 0) {
                info.videoWidth = par->width;
                info.videoHeight = par->height;
                info.videoBitrate = par->bit_rate;
                info.videoCodecId = par->codec_id;
            }
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            info.audioStreamCount++;
            if (info.audioCodecId == AV_CODEC_ID_NONE)
                info.audioCodecId = par->codec_id;
        }
    }

    avformat_close_input(&fmt);
    return info;
}

// Helper: try to decode all frames and check for corruption
bool CanDecodeAllFrames(const std::wstring& path, int maxFrames = 500) {
    int bufSize = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(bufSize, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &utf8[0], bufSize, nullptr, nullptr);
    utf8.resize(bufSize - 1);

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, utf8.c_str(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    int vidIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vidIdx = i;
            break;
        }
    }
    if (vidIdx < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(fmt->streams[vidIdx]->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return false;
    }

    AVCodecContext* cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cc, fmt->streams[vidIdx]->codecpar);
    if (avcodec_open2(cc, codec, nullptr) < 0) {
        avcodec_free_context(&cc);
        avformat_close_input(&fmt);
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frm = av_frame_alloc();
    int decodedFrames = 0;
    bool ok = true;

    while (av_read_frame(fmt, pkt) >= 0 && decodedFrames < maxFrames) {
        if (pkt->stream_index == vidIdx) {
            if (avcodec_send_packet(cc, pkt) < 0) { ok = false; break; }
            while (avcodec_receive_frame(cc, frm) >= 0) {
                decodedFrames++;
                if (frm->width <= 0 || frm->height <= 0) { ok = false; break; }
                av_frame_unref(frm);
            }
        }
        av_packet_unref(pkt);
        if (!ok) break;
    }

    // Flush decoder
    avcodec_send_packet(cc, nullptr);
    while (avcodec_receive_frame(cc, frm) >= 0) {
        decodedFrames++;
        av_frame_unref(frm);
    }

    av_frame_free(&frm);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);
    return ok && decodedFrames > 0;
}

std::wstring MakeOutputPath(const std::wstring& name) {
    return g_testTempDir + L"\\" + name;
}

} // anonymous namespace

void RegisterCuttingTests(TestSuite& suite) {

    // ---- Basic Codec Copy Cut ----
    suite.addTest("Cut_CopyCodec", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"cut_copy.mp4");
        std::atomic<bool> cancel{false};
        bool result = player.CutVideo(out, 1.0, 4.0, false, false,
                                       EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);
        TEST_ASSERT(result, "CutVideo codec copy should succeed");

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT(info.valid, "Output file should be valid");
        TEST_ASSERT_GT(info.videoStreamCount, 0, "Should have video stream");
        TEST_ASSERT_GT(info.audioStreamCount, 0, "Should have audio stream");
        // Duration check: should be roughly 3s (1.0 to 4.0), with tolerance for keyframe alignment
        TEST_ASSERT_GT(info.duration, 1.5, "Copy-codec output duration should be > 1.5s");
        TEST_ASSERT_LT(info.duration, 5.0, "Copy-codec output duration should be < 5.0s");
    });

    // ---- H.264 Re-encode ----
    suite.addTest("Cut_ConvertH264", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"cut_h264.mp4");
        std::atomic<bool> cancel{false};
        bool result = player.CutVideo(out, 1.0, 4.0, false, true,
                                       EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);
        TEST_ASSERT(result, "CutVideo H.264 re-encode should succeed");

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT(info.valid, "Output file should be valid");
        TEST_ASSERT_EQ(static_cast<int>(info.videoCodecId), static_cast<int>(AV_CODEC_ID_H264),
                        "Output video codec should be H.264");
        TEST_ASSERT_NEAR(info.duration, 3.0, 1.0, "H.264 output duration should be ~3s");
    });

    // ---- H.264 with Bitrate Target ----
    suite.addTest("Cut_H264_Bitrate", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"cut_h264_bitrate.mp4");
        std::atomic<bool> cancel{false};
        bool result = player.CutVideo(out, 0.0, 3.0, false, true,
                                       EncoderSelection::Libx264, L"Medium", 2000, nullptr, &cancel);
        TEST_ASSERT(result, "CutVideo with bitrate should succeed");

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT(info.valid, "Output should be valid");
        // File size check: with 2000 kbps for ~3 seconds, expect < ~1MB
        auto fileSize = std::filesystem::file_size(out);
        TEST_ASSERT_GT(static_cast<int64_t>(fileSize), (int64_t)10000, "Output should have non-trivial size");
    });

    // ---- Merge Audio ----
    suite.addTest("Cut_MergeAudio", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"cut_merge_audio.mp4");
        std::atomic<bool> cancel{false};
        bool result = player.CutVideo(out, 0.5, 3.5, true, false,
                                       EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);
        TEST_ASSERT(result, "CutVideo with merge audio should succeed");

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT(info.valid, "Output should be valid");
        TEST_ASSERT_EQ(info.audioStreamCount, 1, "Merged audio should produce exactly 1 audio stream");
    });

    // ---- No Merge Audio (preserves tracks) ----
    suite.addTest("Cut_NoMergeAudio", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"cut_no_merge.mp4");
        std::atomic<bool> cancel{false};
        bool result = player.CutVideo(out, 0.5, 3.5, false, false,
                                       EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);
        TEST_ASSERT(result, "CutVideo without merge should succeed");

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT(info.valid, "Output should be valid");
        TEST_ASSERT_GE(info.audioStreamCount, 1, "Should have at least 1 audio stream");
    });

    // ---- Cut With Crop ----
    suite.addTest("Cut_WithCrop", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);

        // Add a crop keyframe at start
        RECT cropRect = {100, 100, 900, 500}; // 800x400
        player.AddCropKeyframe(0.0, cropRect);

        std::wstring out = MakeOutputPath(L"cut_cropped.mp4");
        std::atomic<bool> cancel{false};
        // Crop forces H.264 conversion
        bool result = player.CutVideo(out, 0.0, 3.0, false, true,
                                       EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);
        TEST_ASSERT(result, "CutVideo with crop should succeed");

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT(info.valid, "Cropped output should be valid");
        // Dimensions should match crop output
        int expectedW = player.GetCropOutputWidth();
        int expectedH = player.GetCropOutputHeight();
        TEST_ASSERT_EQ(info.videoWidth, expectedW, "Width should match crop dimensions");
        TEST_ASSERT_EQ(info.videoHeight, expectedH, "Height should match crop dimensions");
    });

    // ---- Full Duration Export ----
    suite.addTest("Cut_FullDuration", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        double dur = player.GetDuration();
        std::wstring out = MakeOutputPath(L"cut_full.mp4");
        std::atomic<bool> cancel{false};
        bool result = player.CutVideo(out, 0.0, dur, false, false,
                                       EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);
        TEST_ASSERT(result, "Full duration export should succeed");

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT(info.valid, "Full export should be valid");
        TEST_ASSERT_NEAR(info.duration, dur, 1.0, "Output duration should match input");
    });

    // ---- Cancel Midway ----
    suite.addTest("Cut_CancelMidway", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"cut_cancelled.mp4");
        std::atomic<bool> cancel{false};

        // Cancel after a very short delay
        std::thread cancelThread([&cancel]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            cancel = true;
        });

        bool result = player.CutVideo(out, 0.0, player.GetDuration(), false, true,
                                       EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);
        cancelThread.join();

        // Cancelled cut should return false
        TEST_ASSERT(!result, "Cancelled cut should return false");
    });

    // ---- Output Quality: Has Video Stream ----
    suite.addTest("Output_HasVideoStream", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"quality_video.mp4");
        std::atomic<bool> cancel{false};
        player.CutVideo(out, 0.0, 3.0, false, true,
                         EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT(info.valid, "Output should be valid");
        TEST_ASSERT_GT(info.videoStreamCount, 0, "Output must have at least 1 video stream");
    });

    // ---- Output Quality: Has Audio Stream ----
    suite.addTest("Output_HasAudioStream", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"quality_audio.mp4");
        std::atomic<bool> cancel{false};
        player.CutVideo(out, 0.0, 3.0, false, false,
                         EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT(info.valid, "Output should be valid");
        TEST_ASSERT_GT(info.audioStreamCount, 0, "Output must have at least 1 audio stream");
    });

    // ---- Output Quality: Is Playable (can decode at least 1 frame) ----
    suite.addTest("Output_IsPlayable", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"quality_playable.mp4");
        std::atomic<bool> cancel{false};
        player.CutVideo(out, 0.5, 3.5, false, true,
                         EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);

        bool canDecode = CanDecodeAllFrames(out, 5); // Just try first 5 frames
        TEST_ASSERT(canDecode, "Output should be decodable (at least 1 frame)");
    });

    // ---- Output Quality: Frame Dimensions Match ----
    suite.addTest("Output_FrameDimensions", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"quality_dims.mp4");
        std::atomic<bool> cancel{false};
        player.CutVideo(out, 0.0, 2.0, false, true,
                         EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT_EQ(info.videoWidth, 1280, "Output width should be 1280");
        TEST_ASSERT_EQ(info.videoHeight, 720, "Output height should be 720");
    });

    // ---- Output Quality: Duration Accuracy ----
    suite.addTest("Output_Duration_Accurate", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"quality_duration.mp4");
        std::atomic<bool> cancel{false};
        player.CutVideo(out, 1.0, 4.0, false, true,
                         EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT_NEAR(info.duration, 3.0, 0.5, "Output duration should be ~3.0s (±0.5s)");
    });

    // ---- Output Quality: No Corruption (full decode pass) ----
    suite.addTest("Output_NoCorruption", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"quality_nocorrupt.mp4");
        std::atomic<bool> cancel{false};
        player.CutVideo(out, 0.0, player.GetDuration(), false, true,
                         EncoderSelection::Libx264, L"Medium", 0, nullptr, &cancel);

        bool allFramesOk = CanDecodeAllFrames(out, 300);
        TEST_ASSERT(allFramesOk, "All frames should decode without errors");
    });

    // ---- H.264 with different quality presets ----
    suite.addTest("Cut_H264_LowQuality", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"cut_h264_low.mp4");
        std::atomic<bool> cancel{false};
        bool result = player.CutVideo(out, 0.0, 2.0, false, true,
                                       EncoderSelection::Libx264, L"Low", 0, nullptr, &cancel);
        TEST_ASSERT(result, "Low quality H.264 cut should succeed");
        TEST_ASSERT(InspectOutputFile(out).valid, "Low quality output should be valid");
    });

    suite.addTest("Cut_H264_HighQuality", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"cut_h264_high.mp4");
        std::atomic<bool> cancel{false};
        bool result = player.CutVideo(out, 0.0, 2.0, false, true,
                                       EncoderSelection::Libx264, L"High", 0, nullptr, &cancel);
        TEST_ASSERT(result, "High quality H.264 cut should succeed");
        TEST_ASSERT(InspectOutputFile(out).valid, "High quality output should be valid");
    });

    // ---- Merge Audio + H.264 combo ----
    suite.addTest("Cut_MergeAudio_H264", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::wstring out = MakeOutputPath(L"cut_merge_h264.mp4");
        std::atomic<bool> cancel{false};
        bool result = player.CutVideo(out, 0.5, 3.5, true, true,
                                       EncoderSelection::Libx264, L"Medium", 2000, nullptr, &cancel);
        TEST_ASSERT(result, "Merge audio + H.264 cut should succeed");

        OutputInfo info = InspectOutputFile(out);
        TEST_ASSERT(info.valid, "Combined output should be valid");
        TEST_ASSERT_EQ(static_cast<int>(info.videoCodecId), static_cast<int>(AV_CODEC_ID_H264),
                        "Should be H.264");
        TEST_ASSERT_EQ(info.audioStreamCount, 1, "Merged audio should be 1 stream");
    });
}
