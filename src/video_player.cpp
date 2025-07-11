#include "video_player.h"
#include "video_decoder.h"
#include "audio_player.h"
#include "video_renderer.h"
#include "video_cutter.h"
#include "options_window.h"
#include <iostream>
#include <windows.h>
#include <d2d1.h>
#pragma comment(lib, "d2d1.lib")
#include <uxtheme.h>
#include <algorithm>
#include <cstring>
#include <chrono>

VideoPlayer::VideoPlayer(HWND parent)
    : parentWindow(parent), formatContext(nullptr), codecContext(nullptr),
      frame(nullptr), frameRGB(nullptr), hwFrame(nullptr), hwDeviceCtx(nullptr),
      hwPixelFormat(AV_PIX_FMT_NONE), useHwAccel(false), packet(nullptr), swsContext(nullptr),
      buffer(nullptr), videoStreamIndex(-1), frameWidth(0), frameHeight(0),
      isLoaded(false), isPlaying(false), frameRate(0), currentFrame(0),
      totalFrames(0), currentPts(0.0), duration(0.0), startTimeOffset(0.0), videoWindow(nullptr),
      d2dFactory(nullptr), d2dRenderTarget(nullptr), d2dBitmap(nullptr), playbackTimer(0),
      deviceEnumerator(nullptr), audioDevice(nullptr), audioClient(nullptr),
      renderClient(nullptr), audioFormat(nullptr), bufferFrameCount(0),
      audioInitialized(false), audioThreadRunning(false),
      playbackThreadRunning(false),
      audioSampleRate(44100), audioChannels(2), audioSampleFormat(AV_SAMPLE_FMT_S16),
      originalVideoWndProc(nullptr)
{
    m_decoder = std::make_unique<VideoDecoder>(this);
    m_audioPlayer = std::make_unique<AudioPlayer>(this);
    m_renderer = std::make_unique<VideoRenderer>(this);
    m_cutter = std::make_unique<VideoCutter>(this);

    m_renderer->Initialize();
    CreateVideoWindow();
    m_audioPlayer->Initialize();
}

VideoPlayer::~VideoPlayer()
{
    UnloadVideo();
    m_audioPlayer->Cleanup();
    m_renderer->Cleanup();
    if (playbackThreadRunning)
    {
        playbackThreadRunning = false;
        if (playbackThread.joinable())
            playbackThread.join();
    }
    if (videoWindow)
    {
        SetWindowLongPtr(videoWindow, GWLP_WNDPROC, (LONG_PTR)originalVideoWndProc);
        SetWindowLongPtr(videoWindow, GWLP_USERDATA, 0);
        DestroyWindow(videoWindow);
        originalVideoWndProc = nullptr;
    }
}

void VideoPlayer::CreateVideoWindow()
{
    videoWindow = CreateWindow(
        L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
        10, 10, 640, 480,
        parentWindow, nullptr,
        (HINSTANCE)GetWindowLongPtr(parentWindow, GWLP_HINSTANCE),
        nullptr);
    if (videoWindow)
    {
        SetWindowLongPtr(videoWindow, GWLP_USERDATA, (LONG_PTR)this);
        originalVideoWndProc = (WNDPROC)SetWindowLongPtr(videoWindow, GWLP_WNDPROC, (LONG_PTR)VideoWindowProc);
        SetWindowTheme(videoWindow, L"DarkMode_Explorer", nullptr);
        m_renderer->CreateRenderTarget();
    }
}

bool VideoPlayer::LoadVideo(const std::wstring &filename)
{
    UnloadVideo();
    loadedFilename = filename;

    int bufSize = WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8Filename(bufSize, 0);
    WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, &utf8Filename[0], bufSize, nullptr, nullptr);

    formatContext = avformat_alloc_context();
    if (avformat_open_input(&formatContext, utf8Filename.c_str(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(formatContext, nullptr) < 0)
    {
        avformat_close_input(&formatContext);
        return false;
    }

    videoStreamIndex = -1;
    for (unsigned i = 0; i < formatContext->nb_streams; i++)
    {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStreamIndex = i;
            break;
        }
    }
    if (videoStreamIndex < 0)
    {
        avformat_close_input(&formatContext);
        return false;
    }

    if (!m_decoder->Initialize())
    {
        UnloadVideo();
        return false;
    }

    // Determine the earliest stream start time for synchronization
    startTimeOffset = 0.0;
    double minStart = std::numeric_limits<double>::max();
    for (unsigned i = 0; i < formatContext->nb_streams; ++i)
    {
        AVStream *s = formatContext->streams[i];
        if (s->start_time != AV_NOPTS_VALUE)
        {
            double t = s->start_time * av_q2d(s->time_base);
            if (t < minStart)
                minStart = t;
        }
    }
    if (minStart != std::numeric_limits<double>::max())
        startTimeOffset = minStart;

    // Initialize audio tracks
    if (!m_audioPlayer->InitializeTracks())
    {
        std::cout << "Warning: Failed to initialize audio tracks" << std::endl;
    }

    isLoaded = true;
    currentFrame = 0;
    AVStream *vs = formatContext->streams[videoStreamIndex];
    AVRational guessed = av_guess_frame_rate(formatContext, vs, nullptr);
    frameRate = guessed.num && guessed.den ? av_q2d(guessed) : 0.0;
    if (frameRate <= 0.0)
        frameRate = av_q2d(vs->avg_frame_rate);
    if (frameRate <= 0.0)
        frameRate = av_q2d(vs->r_frame_rate);
    if (frameRate <= 0.0 && vs->time_base.den)
        frameRate = (double)vs->time_base.den / vs->time_base.num;
    if (frameRate <= 0.0)
        frameRate = 30.0; // Fallback

    totalFrames = vs->nb_frames
                    ? vs->nb_frames
                    : (vs->duration != AV_NOPTS_VALUE
                           ? (int64_t)(av_q2d(vs->time_base) * vs->duration * frameRate)
                           : 0);

    if (formatContext->duration != AV_NOPTS_VALUE)
        duration = formatContext->duration / (double)AV_TIME_BASE;
    else if (vs->duration != AV_NOPTS_VALUE)
        duration = av_q2d(vs->time_base) * vs->duration;
    else if (totalFrames > 0 && frameRate > 0)
        duration = totalFrames / frameRate;
    else
        duration = 0.0;
    currentPts = 0.0;
    return true;
}

void VideoPlayer::UnloadVideo()
{
    Stop();
    m_audioPlayer->CleanupTracks();
    m_decoder->Cleanup();
    if (formatContext)
    {
        avformat_close_input(&formatContext);
        formatContext = nullptr;
    }
    isLoaded = false;
    videoStreamIndex = -1;
    currentFrame = 0;
    currentPts = 0.0;
    totalFrames = 0;
    duration = 0.0;
}

bool VideoPlayer::Play()
{
    if (!isLoaded || isPlaying)
        return false;
    isPlaying = true;

    masterStartPts = currentPts;
    masterStartTime = std::chrono::high_resolution_clock::now();

    m_audioPlayer->StartThread();
    playbackThreadRunning = true;
    playbackThread = std::thread(&VideoPlayer::PlaybackThreadFunction, this);
    return true;
}

void VideoPlayer::Pause()
{
    if (isPlaying)
    {
        isPlaying = false;
        
        m_audioPlayer->StopThread();
        
        if (playbackThreadRunning)
        {
            playbackThreadRunning = false;
            if (playbackThread.joinable())
            {
                if (std::this_thread::get_id() == playbackThread.get_id())
                    playbackThread.detach();
                else
                    playbackThread.join();
            }
        }
    }
}

void VideoPlayer::Stop()
{
    Pause();
    currentFrame = 0;
    if (isLoaded)
    {
        av_seek_frame(formatContext, videoStreamIndex, 0, AVSEEK_FLAG_FRAME);
        avcodec_flush_buffers(codecContext);
        
        // Flush audio codec buffers
        for (auto& track : audioTracks)
        {
            if (track->codecContext)
                avcodec_flush_buffers(track->codecContext);
        }
        
        // Clear audio buffers
        std::lock_guard<std::mutex> lock(audioMutex);
        for (auto& tr : audioTracks)
            tr->buffer.clear();
    }
}

void VideoPlayer::SeekToFrame(int64_t frameNumber)
{
    if (!isLoaded || frameNumber < 0 || frameNumber >= totalFrames)
        return;

    double seconds = frameRate > 0 ? (frameNumber / frameRate) : 0.0;
    SeekToTime(seconds);
}

void VideoPlayer::SeekToTime(double seconds)
{
    if (!isLoaded)
        return;

    {
        std::lock_guard<std::mutex> lock(decodeMutex);

        AVStream *vs = formatContext->streams[videoStreamIndex];
        int64_t ts = (int64_t)((seconds + startTimeOffset) / av_q2d(vs->time_base));

        // Seek directly to the requested timestamp. AVSEEK_FLAG_ANY allows seeking
        // to non-keyframes so the timeline jumps exactly where the user clicked
        // without having to decode many frames.
        av_seek_frame(formatContext, videoStreamIndex, ts,
                        AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
        avcodec_flush_buffers(codecContext);
        for (auto &track : audioTracks)
        {
            if (track->codecContext)
                avcodec_flush_buffers(track->codecContext);
        }
        {
            std::lock_guard<std::mutex> lock(audioMutex);
            for (auto& tr : audioTracks)
                tr->buffer.clear();
        }

        currentFrame = (int64_t)(seconds * frameRate);
        currentPts = seconds;
    }

    // Decode a few frames after seeking so the display updates immediately
    for (int i = 0; i < 3; ++i)
    {
        if (!m_decoder->DecodeNextFrame(true))
            break;
        if (currentPts >= seconds)
            break;
    }
}

double VideoPlayer::GetDuration() const
{
    return isLoaded ? duration : 0.0;
}

double VideoPlayer::GetCurrentTime() const
{
    return currentPts;
}

void VideoPlayer::SetPosition(int x, int y, int width, int height)
{
    m_renderer->SetPosition(x, y, width, height);
}

void VideoPlayer::Render()
{
    m_renderer->Render();
}

void CALLBACK VideoPlayer::TimerProc(HWND hwnd, UINT, UINT_PTR, DWORD)
{
    VideoPlayer *player = (VideoPlayer *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (player && player->isPlaying)
        player->m_decoder->DecodeNextFrame(true);
}

void VideoPlayer::OnTimer()
{
    if (isPlaying)
        m_decoder->DecodeNextFrame(true);
}

// Audio track management methods
std::string VideoPlayer::GetAudioTrackName(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(audioTracks.size()))
        return "";
    return audioTracks[trackIndex]->name;
}

bool VideoPlayer::IsAudioTrackMuted(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(audioTracks.size()))
        return false;
    return audioTracks[trackIndex]->isMuted;
}

void VideoPlayer::SetAudioTrackMuted(int trackIndex, bool muted)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(audioTracks.size()))
        return;
    audioTracks[trackIndex]->isMuted = muted;
}

float VideoPlayer::GetAudioTrackVolume(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(audioTracks.size()))
        return 0.0f;
    return audioTracks[trackIndex]->volume;
}

void VideoPlayer::SetAudioTrackVolume(int trackIndex, float volume)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(audioTracks.size()))
        return;
    float clampedVolume = volume < 0.0f ? 0.0f : (volume > 2.0f ? 2.0f : volume);
    audioTracks[trackIndex]->volume = clampedVolume;
}

void VideoPlayer::SetMasterVolume(float volume)
{
    m_audioPlayer->SetMasterVolume(volume);
}

void VideoPlayer::PlaybackThreadFunction()
{
    auto startTime = masterStartTime;
    double startPts = masterStartPts;
    while (playbackThreadRunning)
    {
        if (!m_decoder->DecodeNextFrame(false))
            break;

        double target = currentPts - startPts;
        double elapsed = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - startTime).count();
        double delay = target - elapsed;
        if (delay > 0)
            std::this_thread::sleep_for(std::chrono::duration<double>(delay));
    }
    isPlaying = false;
}

bool VideoPlayer::CutVideo(const std::wstring &outputFilename, double startTime,
                           double endTime, bool mergeAudio, bool convertH264,
                           bool useNvenc, int maxBitrate, HWND progressBar,
                           std::atomic<bool>* cancelFlag)
{
    return m_cutter->CutVideo(outputFilename, startTime, endTime, mergeAudio, convertH264, useNvenc, maxBitrate, progressBar, cancelFlag);
}

LRESULT CALLBACK VideoPlayer::VideoWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    VideoPlayer* player = reinterpret_cast<VideoPlayer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (player)
    {
        if (msg == WM_PAINT)
        {
            player->m_renderer->OnVideoWindowPaint();
            return 0;
        }
        else if (msg == WM_ERASEBKGND)
        {
            return 1;
        }
        return CallWindowProc(player->originalVideoWndProc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}