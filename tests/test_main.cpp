// test_main.cpp — Test runner entry point for VideoEditorTests
//
// Creates a hidden window (required by VideoPlayer for Direct2D),
// generates a synthetic test video, runs all test suites, and reports results.

#include "test_framework.h"
#include "../src/video_player.h"
#include "../src/options_window.h"
#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <iostream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/version.h>
}

// Global test state — shared with test files
std::wstring g_testVideoPath;
std::wstring g_testLongVideoPath;
std::wstring g_testHeavyVideoPath;
std::wstring g_testTempDir;
HWND g_testHwnd = nullptr;

// These globals are required by the main app source files we link against.
// They are normally set in main.cpp / window_proc.cpp / ui_controls.cpp.
// We provide stub values so linking succeeds.
HWND g_hButtonOpen = nullptr, g_hButtonPlay = nullptr, g_hButtonPause = nullptr, g_hButtonStop = nullptr;
HWND g_hTimeline = nullptr;
HWND g_hTimelineResizeBar = nullptr;
HWND g_hStatusText = nullptr;
HWND g_hListBoxAudioTracks = nullptr, g_hButtonMuteTrack = nullptr;
HWND g_hSliderTrackVolume = nullptr, g_hSliderMasterVolume = nullptr;
HWND g_hLabelAudioTracks = nullptr, g_hLabelTrackVolume = nullptr;
HWND g_hLabelMasterVolume = nullptr, g_hLabelEditing = nullptr;
HWND g_hButtonSetStart = nullptr, g_hButtonSetEnd = nullptr;
HWND g_hButtonCut = nullptr, g_hCheckboxMergeAudio = nullptr;
HWND g_hRadioCopyCodec = nullptr, g_hRadioH264 = nullptr, g_hEditBitrate = nullptr;
HWND g_hRadioUseBitrate = nullptr, g_hRadioUseSize = nullptr;
HWND g_hLabelBitrate = nullptr;
HWND g_hEditTargetSize = nullptr, g_hLabelTargetSize = nullptr;
HWND g_hEditStartTime = nullptr, g_hEditEndTime = nullptr;
HWND g_hButtonPlayClip = nullptr, g_hButtonPlayEnd = nullptr;
HWND g_hLabelCutInfo = nullptr;
HWND g_hButtonOptions = nullptr;
HWND g_hButtonTogglePanel = nullptr;
HWND g_hButtonAddClip = nullptr, g_hButtonClearClips = nullptr;
HWND g_hListBoxCutSegments = nullptr, g_hButtonUpdateClip = nullptr;
HWND g_hButtonRemoveClip = nullptr, g_hButtonPlayAllClips = nullptr;
HWND g_hButtonSpeedDown = nullptr, g_hButtonSpeedUp = nullptr;
HWND g_hEditPlaybackSpeed = nullptr;
bool g_isPanelVisible = false;
double g_cutStartTime = -1.0;
double g_cutEndTime = -1.0;
std::vector<ClipSegment> g_cutSegments;
int g_selectedCutSegment = -1;
bool g_isTimelineDragging = false;
bool g_wasPlayingBeforeDrag = false;
bool g_resumePlayAfterSeek = false;
enum class DragMode { None, Cursor, StartMarker, EndMarker, Keyframe };
DragMode g_timelineDragMode = DragMode::None;
double g_draggedKeyframeTime = -1.0;
VideoPlayer* g_videoPlayer = nullptr;
HFONT g_hFont = nullptr;
HBRUSH g_hbrBackground = nullptr;
COLORREF g_textColor = RGB(240, 240, 240);

// Forward declarations for test registration
void RegisterUtilsTests(TestSuite& suite);
void RegisterVideoLoadingTests(TestSuite& suite);
void RegisterPlaybackTests(TestSuite& suite);
void RegisterAudioTests(TestSuite& suite);
void RegisterCropTests(TestSuite& suite);
void RegisterCuttingTests(TestSuite& suite);
void RegisterThumbnailTests(TestSuite& suite);

// Hidden window procedure — does nothing
static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Create a hidden window for VideoPlayer initialization
static HWND CreateHiddenWindow(HINSTANCE hInstance) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TestHiddenWindowClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, L"TestHiddenWindowClass", L"Test Window",
        WS_OVERLAPPEDWINDOW,
        0, 0, 800, 600,
        nullptr, nullptr, hInstance, nullptr);

    return hwnd;
}

// Generate a synthetic test video using FFmpeg lavfi sources
// This creates a 5-second 1280×720 30fps video with 2 audio tracks
static bool GenerateTestVideo(const std::wstring& outputPath, const std::wstring& ffmpegDir) {
    std::wcout << L"  Generating test video: " << outputPath << std::endl;

    // Try to find ffmpeg.exe
    std::wstring ffmpegExe;
    
    // Check in third_party/ffmpeg/bin
    std::wstring thirdParty = ffmpegDir + L"\\bin\\ffmpeg.exe";
    if (std::filesystem::exists(thirdParty)) {
        ffmpegExe = L"\"" + thirdParty + L"\"";
    } else {
        // Try PATH
        ffmpegExe = L"ffmpeg";
    }

    // Build command:
    // - testsrc2: synthetic video source with changing patterns (1280x720, 30fps)
    // - sine: 2 audio tracks at 440Hz and 880Hz
    // - Duration: 5 seconds
    std::wstring cmd = ffmpegExe +
        L" -y -f lavfi -i \"testsrc2=size=1280x720:rate=30:duration=5\" "
        L"-f lavfi -i \"sine=frequency=440:sample_rate=44100:duration=5\" "
        L"-f lavfi -i \"sine=frequency=880:sample_rate=44100:duration=5\" "
        L"-map 0:v -map 1:a -map 2:a "
        L"-c:v libx264 -preset ultrafast -crf 23 -bf 2 -g 60 "
        L"-c:a aac -b:a 128k "
        L"-shortest \"" + outputPath + L"\"";

    // Run the command
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    std::wstring cmdLine = cmd;
    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        std::wcout << L"  Warning: Could not run ffmpeg. Error: " << GetLastError() << std::endl;
        return false;
    }

    WaitForSingleObject(pi.hProcess, 30000); // 30s timeout
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
        std::wcout << L"  Warning: ffmpeg exited with code " << exitCode << std::endl;
        return false;
    }

    return std::filesystem::exists(outputPath);
}

// A longer, low-resolution input keeps very-high-speed tests away from EOF.
// It deliberately includes B-frames and frequent keyframes so the playback
// catch-up path is tested against a normal inter-frame dependency structure.
static bool GenerateLongSpeedTestVideo(const std::wstring& outputPath,
                                       const std::wstring& ffmpegDir) {
    std::wstring ffmpegExe;
    const std::wstring bundledExe = ffmpegDir + L"\\bin\\ffmpeg.exe";
    if (!ffmpegDir.empty() && std::filesystem::exists(bundledExe))
        ffmpegExe = L"\"" + bundledExe + L"\"";
    else
        ffmpegExe = L"ffmpeg";

    std::wstring cmd = ffmpegExe +
        L" -y -f lavfi -i \"testsrc2=size=320x180:rate=30:duration=240\" "
        L"-an -c:v libx264 -preset ultrafast -crf 28 -bf 2 "
        L"-g 30 -keyint_min 30 -sc_threshold 0 \"" + outputPath + L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;

    const DWORD waitResult = WaitForSingleObject(pi.hProcess, 30000);
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0)
        GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode == 0 && std::filesystem::exists(outputPath);
}

// Deliberately exceeds sequential 10x decode throughput on typical hardware.
// The 4:4:4 10-bit profile also avoids silently making the regression easy via
// the common consumer H.264 hardware-decode path.
static bool GenerateHeavySpeedTestVideo(const std::wstring& outputPath,
                                        const std::wstring& ffmpegDir) {
    std::wstring ffmpegExe;
    const std::wstring bundledExe = ffmpegDir + L"\\bin\\ffmpeg.exe";
    if (!ffmpegDir.empty() && std::filesystem::exists(bundledExe))
        ffmpegExe = L"\"" + bundledExe + L"\"";
    else
        ffmpegExe = L"ffmpeg";

    std::wstring cmd = ffmpegExe +
        L" -y -hide_banner -loglevel error "
        L"-f lavfi -i \"testsrc2=size=3840x2160:rate=60:duration=20\" "
        L"-an -c:v libx264 -preset ultrafast -crf 34 -pix_fmt yuv444p10le "
        L"-bf 3 -g 120 -keyint_min 120 -sc_threshold 0 \"" + outputPath + L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;

    const DWORD waitResult = WaitForSingleObject(pi.hProcess, 30000);
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0)
        GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode == 0 && std::filesystem::exists(outputPath);
}

// Generate test video using FFmpeg API directly (fallback if no ffmpeg.exe)
static bool GenerateTestVideoAPI(const std::wstring& outputPath) {
    std::wcout << L"  Generating test video via API: " << outputPath << std::endl;

    int bufSize = WideCharToMultiByte(CP_UTF8, 0, outputPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(bufSize, 0);
    WideCharToMultiByte(CP_UTF8, 0, outputPath.c_str(), -1, &utf8[0], bufSize, nullptr, nullptr);
    utf8.resize(bufSize - 1);

    // Use lavfi input to generate a test pattern
    AVFormatContext* ifmt = nullptr;
    AVDictionary* opts = nullptr;

    // Open a lavfi testsrc2
    const AVInputFormat* lavfi = av_find_input_format("lavfi");
    if (!lavfi) {
        std::cout << "  lavfi input format not available" << std::endl;
        return false;
    }

    av_dict_set(&opts, "video_size", "1280x720", 0);
    av_dict_set(&opts, "framerate", "30", 0);
    av_dict_set(&opts, "duration", "5", 0);

    if (avformat_open_input(&ifmt, "testsrc2", lavfi, &opts) < 0) {
        av_dict_free(&opts);
        std::cout << "  Failed to open lavfi testsrc2" << std::endl;
        return false;
    }
    av_dict_free(&opts);

    if (avformat_find_stream_info(ifmt, nullptr) < 0) {
        avformat_close_input(&ifmt);
        return false;
    }

    // Output
    AVFormatContext* ofmt = nullptr;
    if (avformat_alloc_output_context2(&ofmt, nullptr, nullptr, utf8.c_str()) < 0) {
        avformat_close_input(&ifmt);
        return false;
    }

    // Video encoder
    const AVCodec* venc = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!venc) {
        avformat_close_input(&ifmt);
        avformat_free_context(ofmt);
        return false;
    }

    AVStream* vout = avformat_new_stream(ofmt, venc);
    AVCodecContext* vcc = avcodec_alloc_context3(venc);
    vcc->width = 1280;
    vcc->height = 720;
    vcc->time_base = {1, 30};
    vcc->framerate = {30, 1};
    vcc->pix_fmt = AV_PIX_FMT_YUV420P;
    vcc->gop_size = 12;
    vcc->max_b_frames = 0;

    AVDictionary* encOpts = nullptr;
    av_dict_set(&encOpts, "preset", "ultrafast", 0);
    av_dict_set(&encOpts, "crf", "28", 0);

    if (ofmt->oformat->flags & AVFMT_GLOBALHEADER)
        vcc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(vcc, venc, &encOpts) < 0) {
        av_dict_free(&encOpts);
        avcodec_free_context(&vcc);
        avformat_close_input(&ifmt);
        avformat_free_context(ofmt);
        return false;
    }
    av_dict_free(&encOpts);
    avcodec_parameters_from_context(vout->codecpar, vcc);
    vout->time_base = vcc->time_base;

    // Audio encoder
    const AVCodec* aenc = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!aenc) {
        avcodec_free_context(&vcc);
        avformat_close_input(&ifmt);
        avformat_free_context(ofmt);
        return false;
    }

    AVStream* aout = avformat_new_stream(ofmt, aenc);
    AVCodecContext* acc = avcodec_alloc_context3(aenc);
    acc->sample_rate = 44100;
    av_channel_layout_default(&acc->ch_layout, 2);
#if defined(LIBAVCODEC_VERSION_MAJOR) && LIBAVCODEC_VERSION_MAJOR >= 61
    const enum AVSampleFormat *sample_fmts = nullptr;
    int ret = avcodec_get_supported_config(nullptr, aenc, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void **)&sample_fmts, nullptr);
    acc->sample_fmt = (ret >= 0 && sample_fmts && sample_fmts[0] != AV_SAMPLE_FMT_NONE) ? sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
#else
#pragma warning(push)
#pragma warning(disable: 4996)
    acc->sample_fmt = aenc->sample_fmts ? aenc->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
#pragma warning(pop)
#endif
    acc->time_base = {1, 44100};
    acc->bit_rate = 128000;
    if (ofmt->oformat->flags & AVFMT_GLOBALHEADER)
        acc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(acc, aenc, nullptr) < 0) {
        avcodec_free_context(&vcc);
        avcodec_free_context(&acc);
        avformat_close_input(&ifmt);
        avformat_free_context(ofmt);
        return false;
    }
    avcodec_parameters_from_context(aout->codecpar, acc);
    aout->time_base = acc->time_base;

    // Open output file
    if (!(ofmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmt->pb, utf8.c_str(), AVIO_FLAG_WRITE) < 0) {
            avcodec_free_context(&vcc);
            avcodec_free_context(&acc);
            avformat_close_input(&ifmt);
            avformat_free_context(ofmt);
            return false;
        }
    }

    if (avformat_write_header(ofmt, nullptr) < 0) {
        avcodec_free_context(&vcc);
        avcodec_free_context(&acc);
        avformat_close_input(&ifmt);
        if (!(ofmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&ofmt->pb);
        avformat_free_context(ofmt);
        return false;
    }

    // Decode from lavfi and re-encode
    AVPacket* ipkt = av_packet_alloc();
    AVPacket* opkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* vframe = av_frame_alloc();

    // Setup video frame
    vframe->format = vcc->pix_fmt;
    vframe->width = vcc->width;
    vframe->height = vcc->height;
    av_frame_get_buffer(vframe, 32);

    // SWS for format conversion
    int inVidIdx = -1;
    for (unsigned i = 0; i < ifmt->nb_streams; i++) {
        if (ifmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            inVidIdx = i; break;
        }
    }

    const AVCodec* idec = avcodec_find_decoder(ifmt->streams[inVidIdx]->codecpar->codec_id);
    AVCodecContext* icc = avcodec_alloc_context3(idec);
    avcodec_parameters_to_context(icc, ifmt->streams[inVidIdx]->codecpar);
    avcodec_open2(icc, idec, nullptr);

    SwsContext* sws = nullptr;
    int64_t vpts = 0;

    // Generate audio: simple sine wave
    AVFrame* aframe = av_frame_alloc();
    aframe->format = acc->sample_fmt;
    av_channel_layout_copy(&aframe->ch_layout, &acc->ch_layout);
    aframe->sample_rate = acc->sample_rate;
    aframe->nb_samples = acc->frame_size > 0 ? acc->frame_size : 1024;
    av_frame_get_buffer(aframe, 0);

    int64_t apts = 0;
    int totalAudioSamples = 44100 * 5; // 5 seconds
    double freq = 440.0;

    // Write audio frames
    while (apts < totalAudioSamples) {
        av_frame_make_writable(aframe);
        int ns = aframe->nb_samples;
        if (apts + ns > totalAudioSamples)
            ns = (int)(totalAudioSamples - apts);
        aframe->nb_samples = ns;

        // Fill with sine wave
        if (acc->sample_fmt == AV_SAMPLE_FMT_FLTP) {
            float* ch0 = (float*)aframe->data[0];
            float* ch1 = aframe->data[1] ? (float*)aframe->data[1] : ch0;
            for (int i = 0; i < ns; i++) {
                float sample = 0.3f * (float)sin(2.0 * 3.14159265 * freq * (apts + i) / 44100.0);
                ch0[i] = sample;
                ch1[i] = sample;
            }
        } else if (acc->sample_fmt == AV_SAMPLE_FMT_S16) {
            int16_t* data = (int16_t*)aframe->data[0];
            for (int i = 0; i < ns * 2; i++) {
                data[i] = (int16_t)(9000.0 * sin(2.0 * 3.14159265 * freq * (apts + i/2) / 44100.0));
            }
        }

        aframe->pts = apts;
        avcodec_send_frame(acc, aframe);
        while (avcodec_receive_packet(acc, opkt) >= 0) {
            av_packet_rescale_ts(opkt, acc->time_base, aout->time_base);
            opkt->stream_index = aout->index;
            av_interleaved_write_frame(ofmt, opkt);
            av_packet_unref(opkt);
        }
        apts += ns;
    }

    // Flush audio encoder
    avcodec_send_frame(acc, nullptr);
    while (avcodec_receive_packet(acc, opkt) >= 0) {
        av_packet_rescale_ts(opkt, acc->time_base, aout->time_base);
        opkt->stream_index = aout->index;
        av_interleaved_write_frame(ofmt, opkt);
        av_packet_unref(opkt);
    }

    // Decode video from lavfi and encode
    while (av_read_frame(ifmt, ipkt) >= 0) {
        if (ipkt->stream_index == inVidIdx) {
            avcodec_send_packet(icc, ipkt);
            while (avcodec_receive_frame(icc, frame) >= 0) {
                if (!sws) {
                    sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                                         vcc->width, vcc->height, vcc->pix_fmt,
                                         SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                }
                av_frame_make_writable(vframe);
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                          vframe->data, vframe->linesize);
                vframe->pts = vpts++;
                avcodec_send_frame(vcc, vframe);
                while (avcodec_receive_packet(vcc, opkt) >= 0) {
                    av_packet_rescale_ts(opkt, vcc->time_base, vout->time_base);
                    opkt->stream_index = vout->index;
                    av_interleaved_write_frame(ofmt, opkt);
                    av_packet_unref(opkt);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(ipkt);
    }

    // Flush video encoder
    avcodec_send_frame(vcc, nullptr);
    while (avcodec_receive_packet(vcc, opkt) >= 0) {
        av_packet_rescale_ts(opkt, vcc->time_base, vout->time_base);
        opkt->stream_index = vout->index;
        av_interleaved_write_frame(ofmt, opkt);
        av_packet_unref(opkt);
    }

    av_write_trailer(ofmt);

    // Cleanup
    if (sws) sws_freeContext(sws);
    av_frame_free(&aframe);
    av_frame_free(&vframe);
    av_frame_free(&frame);
    av_packet_free(&ipkt);
    av_packet_free(&opkt);
    avcodec_free_context(&icc);
    avcodec_free_context(&vcc);
    avcodec_free_context(&acc);
    avformat_close_input(&ifmt);
    if (!(ofmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ofmt->pb);
    avformat_free_context(ofmt);

    return std::filesystem::exists(outputPath);
}

int wmain(int argc, wchar_t* argv[]) {
    // Initialize COM (required by VideoPlayer for WASAPI audio)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Initialize settings with defaults
    LoadSettings();

    // Force some test-friendly settings
    g_logToFile = false;
    g_autoPlay = false;
    g_improveSeekPerformance = true;

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    TestColors::SetCyan();
    std::cout << "============================================" << std::endl;
    std::cout << "  Video Editor Test Suite" << std::endl;
    std::cout << "============================================" << std::endl;
    TestColors::Reset();
    std::cout << std::endl;

    // 1. Create hidden window
    std::cout << "[Setup] Creating hidden window..." << std::endl;
    g_testHwnd = CreateHiddenWindow(hInstance);
    if (!g_testHwnd) {
        TestColors::SetRed();
        std::cerr << "FATAL: Could not create hidden window" << std::endl;
        TestColors::Reset();
        CoUninitialize();
        return 1;
    }

    // 2. Create temp directory for test outputs
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    g_testTempDir = std::wstring(tempPath) + L"VideoEditorTests";
    std::filesystem::create_directories(g_testTempDir);
    std::wcout << L"[Setup] Temp directory: " << g_testTempDir << std::endl;

    // 3. Generate test video
    g_testVideoPath = g_testTempDir + L"\\test_input.mp4";
    std::cout << "[Setup] Generating test video..." << std::endl;

    bool videoGenerated = false;

    // First try using ffmpeg.exe
    std::wstring ffmpegDir;
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        // Check relative to exe (build/Release -> third_party/ffmpeg)
        std::filesystem::path thirdParty = exeDir / ".." / ".." / "third_party" / "ffmpeg";
        if (std::filesystem::exists(thirdParty / "bin" / "ffmpeg.exe")) {
            ffmpegDir = thirdParty.wstring();
        } else {
            // Try project root
            thirdParty = exeDir / "third_party" / "ffmpeg";
            if (std::filesystem::exists(thirdParty / "bin" / "ffmpeg.exe")) {
                ffmpegDir = thirdParty.wstring();
            }
        }
    }

    if (!ffmpegDir.empty()) {
        videoGenerated = GenerateTestVideo(g_testVideoPath, ffmpegDir);
    }

    if (!videoGenerated) {
        std::cout << "[Setup] ffmpeg.exe not found, trying API generation..." << std::endl;
        videoGenerated = GenerateTestVideoAPI(g_testVideoPath);
    }

    if (!videoGenerated) {
        TestColors::SetRed();
        std::cerr << "FATAL: Could not generate test video." << std::endl;
        std::cerr << "  Make sure ffmpeg.exe is in third_party/ffmpeg/bin/ or on PATH." << std::endl;
        TestColors::Reset();
        DestroyWindow(g_testHwnd);
        CoUninitialize();
        return 1;
    }

    g_testLongVideoPath = g_testTempDir + L"\\test_speed_input.mp4";
    std::cout << "[Setup] Generating long high-speed test video..." << std::endl;
    if (!GenerateLongSpeedTestVideo(g_testLongVideoPath, ffmpegDir)) {
        TestColors::SetRed();
        std::cerr << "FATAL: Could not generate long high-speed test video." << std::endl;
        TestColors::Reset();
        DestroyWindow(g_testHwnd);
        CoUninitialize();
        return 1;
    }

    g_testHeavyVideoPath = g_testTempDir + L"\\test_speed_heavy.mp4";
    std::cout << "[Setup] Generating heavy 10x regression video..." << std::endl;
    if (!GenerateHeavySpeedTestVideo(g_testHeavyVideoPath, ffmpegDir)) {
        TestColors::SetRed();
        std::cerr << "FATAL: Could not generate heavy 10x regression video." << std::endl;
        TestColors::Reset();
        DestroyWindow(g_testHwnd);
        CoUninitialize();
        return 1;
    }

    TestColors::SetGreen();
    std::cout << "[Setup] Test video ready." << std::endl;
    TestColors::Reset();
    std::cout << std::endl;

    // 4. Register and run all test suites
    TestRunner runner;

    TestSuite utilsSuite("Utils");
    RegisterUtilsTests(utilsSuite);
    runner.addSuite(&utilsSuite);

    TestSuite loadingSuite("Video Loading");
    RegisterVideoLoadingTests(loadingSuite);
    runner.addSuite(&loadingSuite);

    TestSuite playbackSuite("Playback & Seeking");
    RegisterPlaybackTests(playbackSuite);
    runner.addSuite(&playbackSuite);

    TestSuite audioSuite("Audio Tracks");
    RegisterAudioTests(audioSuite);
    runner.addSuite(&audioSuite);

    TestSuite cropSuite("Crop Keyframes");
    RegisterCropTests(cropSuite);
    runner.addSuite(&cropSuite);

    TestSuite cuttingSuite("Video Cutting & Output Quality");
    RegisterCuttingTests(cuttingSuite);
    runner.addSuite(&cuttingSuite);

    TestSuite thumbnailSuite("Thumbnails");
    RegisterThumbnailTests(thumbnailSuite);
    runner.addSuite(&thumbnailSuite);

    int result = runner.runAll();

    // 5. Cleanup
    std::cout << "[Cleanup] Removing temp files..." << std::endl;
    try {
        std::filesystem::remove_all(g_testTempDir);
    } catch (...) {
        std::cout << "  Warning: Could not clean up temp directory" << std::endl;
    }

    DestroyWindow(g_testHwnd);
    CoUninitialize();

    return result;
}
