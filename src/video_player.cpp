#include "video_player.h"
#include "video_decoder.h"
#include "audio_player.h"
#include "video_renderer.h"
#include "video_cutter.h"
#include "options_window.h"
#include "ui_updates.h"
#include <iostream>
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#pragma comment(lib, "d2d1.lib")
#include <uxtheme.h>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <sstream>
#include <iomanip>

void UpdateControls();

VideoPlayer::VideoPlayer(HWND parent)
    : parentWindow(parent), formatContext(nullptr), codecContext(nullptr),
      frame(nullptr), frameRGB(nullptr), hwFrame(nullptr), hwDeviceCtx(nullptr),
      hwPixelFormat(AV_PIX_FMT_NONE), useHwAccel(false), packet(nullptr), swsContext(nullptr),
      buffer(nullptr), rgbBufferSize(0), videoStreamIndex(-1), frameWidth(0), frameHeight(0),
      isLoaded(false), isPlaying(false), frameRate(0), currentFrame(0),
      totalFrames(0), currentPts(0.0), duration(0.0), startTimeOffset(0.0),
      clipPreviewActive(false), clipPreviewEndTime(0.0),
      cropRect{0,0,0,0}, hasCrop(false), selectingCrop(false), cropStart{0,0}, cropCurrent{0,0},
      videoWindow(nullptr),
      d2dFactory(nullptr), d2dRenderTarget(nullptr), d2dBitmap(nullptr), playbackTimer(0),
      deviceEnumerator(nullptr), audioDevice(nullptr), audioClient(nullptr),
      renderClient(nullptr), audioFormat(nullptr), bufferFrameCount(0),
      audioInitialized(false), audioThreadRunning(false),
      playbackThreadRunning(false),
    audioSampleRate(44100), audioChannels(2), audioSampleFormat(AV_SAMPLE_FMT_S16),
    originalVideoWndProc(nullptr),
      dropAudioDuringStepping(false), frameCacheLimit(50),
      playbackSpeed(1.0), speedTextUntil(std::chrono::steady_clock::time_point::min())
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
    // Use SS_NOTIFY to ensure mouse messages are delivered to our window procedure
    videoWindow = CreateWindow(
        L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | SS_BLACKRECT | SS_NOTIFY,
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
    cropRect = {0,0,0,0};
    cropStack.clear();
    hasCrop = false;
    frameCache.clear();
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
        clipPreviewActive = false;

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
    clipPreviewActive = false;
    Pause();
    currentFrame = 0;
    currentPts = 0.0;
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

void VideoPlayer::PlayClip(double startTime, double endTime)
{
    if (!isLoaded)
        return;
    if (isPlaying)
        Pause();

    clipPreviewEndTime = endTime;
    clipPreviewActive = true;
    SeekToTime(startTime);
    Play();
}

void VideoPlayer::CancelClipPreview()
{
    if (clipPreviewActive)
    {
        clipPreviewActive = false;
        if (isPlaying)
        {
            Pause();
            UpdateControls();
        }
    }
}

void VideoPlayer::ChangePlaybackSpeed(double delta)
{
    double pos = GetCurrentTime();
    playbackSpeed += delta;
    if (playbackSpeed < 0.1)
        playbackSpeed = 0.1;
    if (playbackSpeed > 10.0)
        playbackSpeed = 10.0;
    if (codecContext)
    {
        if (playbackSpeed >= 8.0)
            codecContext->skip_frame = AVDISCARD_NONKEY;
        else if (playbackSpeed >= 2.0)
            codecContext->skip_frame = AVDISCARD_NONREF;
        else
            codecContext->skip_frame = AVDISCARD_DEFAULT;
    }

    bool mute = (playbackSpeed > 3.0) || (playbackSpeed < 0.5);
    for (auto &track : audioTracks)
        track->isMuted = mute;

    auto now = std::chrono::high_resolution_clock::now();
    masterStartPts = currentPts = pos;
    masterStartTime = now;
    if (m_audioPlayer)
        m_audioPlayer->ResetPlaybackPosition();
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(1) << playbackSpeed << L"x";
    speedText = ss.str();
    speedTextUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    if (m_renderer)
        m_renderer->UpdateDisplay();
    audioCondition.notify_all();
}

bool VideoPlayer::IsSpeedTextVisible() const
{
    return !speedText.empty() && std::chrono::steady_clock::now() < speedTextUntil;
}

const std::wstring& VideoPlayer::GetSpeedText() const
{
    return speedText;
}

void VideoPlayer::CheckSpeedDisplay()
{
    if (!speedText.empty() && std::chrono::steady_clock::now() > speedTextUntil)
    {
        speedText.clear();
        if (!isPlaying && m_renderer)
            m_renderer->UpdateDisplay();
    }
}

void VideoPlayer::SeekToFrame(int64_t frameNumber)
{
    if (!isLoaded || frameNumber < 0 || (totalFrames > 0 && frameNumber >= totalFrames))
        return;

    if (frameNumber == currentFrame)
        return;

    // First try to find the frame in the cache for instant display
    // Use binary search for more efficient lookup in large caches
    if (!frameCache.empty())
    {
        // First check if the frame is in cache range
        if (frameNumber >= frameCache.front().number && frameNumber <= frameCache.back().number)
        {
            // Use binary search for more efficient lookup
            auto lower = std::lower_bound(
                frameCache.begin(), 
                frameCache.end(), 
                frameNumber,
                [](const CachedFrame& frame, int64_t num) { return frame.number < num; }
            );
            
            if (lower != frameCache.end() && lower->number == frameNumber)
            {
                // Found exact frame in cache
                std::copy(lower->pixels.begin(), lower->pixels.end(), buffer);
                currentFrame = frameNumber;
                currentPts = lower->pts;
                m_renderer->UpdateDisplay();
                UpdateTimeline();
                return;
            }
        }
    }

    dropAudioDuringStepping = true;

    // Optimize stepping forward one frame by decoding without seeking
    if (frameNumber == currentFrame + 1)
    {
        m_decoder->DecodeNextFrame(true);
        dropAudioDuringStepping = false;
        return;
    }

    // For backwards navigation
    if (frameNumber < currentFrame)
    {
        // If we're moving backwards a significant amount, use keyframe seeking
        int64_t distance = currentFrame - frameNumber;
        
        // If we're moving backwards by more than 5 frames or cache is smaller than the distance
        if (distance > 5 || distance > static_cast<int64_t>(frameCache.size()))
        {
            // Seek to keyframe a bit before the target frame
            double targetTime = frameRate > 0 ? (frameNumber / frameRate) : 0.0;
            
            // Add a safety margin to make sure we get a keyframe before our target
            double seekTime = std::max(0.0, targetTime - 0.5);  // Half second buffer
            
            // Force a keyframe-based seek to minimize decoding
            AVStream *vs = formatContext->streams[videoStreamIndex];
            int64_t ts = static_cast<int64_t>((seekTime + startTimeOffset) / av_q2d(vs->time_base));
            
            {
                std::lock_guard<std::mutex> lock(decodeMutex);
                
                // Clear the frame cache to avoid using stale frames
                frameCache.clear();
                
                // Seek to the keyframe using BACKWARD flag but not ANY flag
                av_seek_frame(formatContext, videoStreamIndex, ts, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(codecContext);
                
                // Reset audio state
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
                
                // Update the current position estimation
                currentPts = seekTime;
                currentFrame = static_cast<int64_t>(currentPts * frameRate);
            }
            
            // Decode frames until we reach our target frame
            while (currentFrame < frameNumber)
            {
                bool last = (currentFrame + 1 >= frameNumber);
                if (!m_decoder->DecodeNextFrame(last, false))
                    break;
            }
        }
        else 
        {
            // For small backwards movements, just step backwards through the cache
            // This should be fast since we're using cached frames
            for (int64_t f = currentFrame - 1; f >= frameNumber; f--)
            {
                bool found = false;
                for (auto it = frameCache.rbegin(); it != frameCache.rend(); ++it)
                {
                    if (it->number == f)
                    {
                        std::copy(it->pixels.begin(), it->pixels.end(), buffer);
                        currentFrame = f;
                        currentPts = it->pts;
                        found = true;
                        break;
                    }
                }
                
                if (!found)
                {
                    // If we can't find a frame in the cache, fall back to seeking
                    double seconds = frameRate > 0 ? (f / frameRate) : 0.0;
                    SeekToTime(seconds, 0);
                    break;
                }
                
                // Only update display for the last frame
                if (f == frameNumber)
                {
                    m_renderer->UpdateDisplay();
                    UpdateTimeline();
                }
            }
        }
    }
    else
    {
        // For forward seeking beyond next frame
        double seconds = frameRate > 0 ? (frameNumber / frameRate) : 0.0;
        SeekToTime(seconds, 0);

        while (currentFrame < frameNumber)
        {
            bool last = (currentFrame + 1 >= frameNumber);
            if (!m_decoder->DecodeNextFrame(last, false))
                break;
        }
    }

    dropAudioDuringStepping = false;
}

void VideoPlayer::SeekToTime(double seconds, int decodeCount)
{
    if (!isLoaded)
        return;

    {
        std::lock_guard<std::mutex> lock(decodeMutex);
        frameCache.clear();

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

        // Estimate the frame and PTS we landed on using the stream index
        const AVIndexEntry *entry =
            avformat_index_get_entry_from_timestamp(vs, ts, AVSEEK_FLAG_BACKWARD);
    if (entry)
        {
            int64_t keyTs = entry->timestamp;
            double keyTime = keyTs * av_q2d(vs->time_base) - startTimeOffset;
            currentPts = keyTime;
            currentFrame = (int64_t)(currentPts * frameRate);
        }
        else
        {
            currentPts = seconds;
            currentFrame = (int64_t)(seconds * frameRate);
        }
    }

    // Decode frames after seeking so the display updates immediately
    for (int i = 0; i < decodeCount; ++i)
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
    double t;
    if (isPlaying)
    {
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - masterStartTime).count();
        t = masterStartPts + elapsed * playbackSpeed;
    }
    else
    {
        t = currentPts;
    }
    if (clipPreviewActive && t > clipPreviewEndTime)
        t = clipPreviewEndTime;
    if (duration > 0.0 && t > duration)
        t = duration;
    return t;
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
    {
        m_decoder->DecodeNextFrame(true);
        if (clipPreviewActive && currentPts >= clipPreviewEndTime)
        {
            clipPreviewActive = false;
            Pause();
            UpdateControls();
        }
    }
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

bool VideoPlayer::IsVoiceIsolationEnabled(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(audioTracks.size()))
        return false;
    return audioTracks[trackIndex]->voiceIsolationEnabled;
}

void VideoPlayer::SetVoiceIsolationEnabled(int trackIndex, bool enabled)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(audioTracks.size()))
        return;
        
    auto& track = audioTracks[trackIndex];
    
    if (enabled && !track->voiceIsolationEnabled)
    {
        // Initialize RNNoise denoiser with default model
        track->denoiseState = rnnoise_create(nullptr);
        if (track->denoiseState)
        {
            // Set up forward resampler: stereo original rate -> mono 48kHz
            track->voiceIsolationSwrContext = swr_alloc();
            if (track->voiceIsolationSwrContext)
            {
                AVChannelLayout in_ch_layout, out_ch_layout;
                av_channel_layout_from_mask(&in_ch_layout, AV_CH_LAYOUT_STEREO);
                av_channel_layout_from_mask(&out_ch_layout, AV_CH_LAYOUT_MONO);
                
                av_opt_set_chlayout(track->voiceIsolationSwrContext, "in_chlayout", &in_ch_layout, 0);
                av_opt_set_chlayout(track->voiceIsolationSwrContext, "out_chlayout", &out_ch_layout, 0);
                av_opt_set_int(track->voiceIsolationSwrContext, "in_sample_rate", audioSampleRate, 0);
                av_opt_set_int(track->voiceIsolationSwrContext, "out_sample_rate", 48000, 0);
                av_opt_set_sample_fmt(track->voiceIsolationSwrContext, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                av_opt_set_sample_fmt(track->voiceIsolationSwrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                
                if (swr_init(track->voiceIsolationSwrContext) >= 0)
                {
                    // Set up backward resampler: mono 48kHz -> stereo original rate
                    track->voiceIsolationBackSwrContext = swr_alloc();
                    if (track->voiceIsolationBackSwrContext)
                    {
                        av_channel_layout_from_mask(&in_ch_layout, AV_CH_LAYOUT_MONO);
                        av_channel_layout_from_mask(&out_ch_layout, AV_CH_LAYOUT_STEREO);
                        
                        av_opt_set_chlayout(track->voiceIsolationBackSwrContext, "in_chlayout", &in_ch_layout, 0);
                        av_opt_set_chlayout(track->voiceIsolationBackSwrContext, "out_chlayout", &out_ch_layout, 0);
                        av_opt_set_int(track->voiceIsolationBackSwrContext, "in_sample_rate", 48000, 0);
                        av_opt_set_int(track->voiceIsolationBackSwrContext, "out_sample_rate", audioSampleRate, 0);
                        av_opt_set_sample_fmt(track->voiceIsolationBackSwrContext, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                        av_opt_set_sample_fmt(track->voiceIsolationBackSwrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                        
                        if (swr_init(track->voiceIsolationBackSwrContext) >= 0)
                        {
                            track->voiceIsolationEnabled = true;
                            // Pre-allocate buffers for RNNoise processing
                            int frameSize = rnnoise_get_frame_size(); // 480 samples
                            track->voiceIsolationInputBuffer.resize(frameSize);
                            track->voiceIsolationMonoBuffer.resize(frameSize * 4); // Extra space for resampling
                            track->voiceIsolationProcessedBuffer.resize(frameSize * 4);
                        }
                        else
                        {
                            swr_free(&track->voiceIsolationBackSwrContext);
                            swr_free(&track->voiceIsolationSwrContext);
                            rnnoise_destroy(track->denoiseState);
                            track->denoiseState = nullptr;
                        }
                    }
                    else
                    {
                        swr_free(&track->voiceIsolationSwrContext);
                        rnnoise_destroy(track->denoiseState);
                        track->denoiseState = nullptr;
                    }
                }
                else
                {
                    swr_free(&track->voiceIsolationSwrContext);
                    rnnoise_destroy(track->denoiseState);
                    track->denoiseState = nullptr;
                }
            }
            else
            {
                rnnoise_destroy(track->denoiseState);
                track->denoiseState = nullptr;
            }
        }
    }
    else if (!enabled && track->voiceIsolationEnabled)
    {
        // Cleanup voice isolation resources
        if (track->voiceIsolationBackSwrContext)
        {
            swr_free(&track->voiceIsolationBackSwrContext);
            track->voiceIsolationBackSwrContext = nullptr;
        }
        if (track->voiceIsolationSwrContext)
        {
            swr_free(&track->voiceIsolationSwrContext);
            track->voiceIsolationSwrContext = nullptr;
        }
        if (track->denoiseState)
        {
            rnnoise_destroy(track->denoiseState);
            track->denoiseState = nullptr;
        }
        track->voiceIsolationEnabled = false;
        track->voiceIsolationInputBuffer.clear();
        track->voiceIsolationMonoBuffer.clear();
        track->voiceIsolationProcessedBuffer.clear();
        track->voiceIsolationSampleQueue.clear();
    }
}

void VideoPlayer::PlaybackThreadFunction()
{
    while (playbackThreadRunning)
    {
        if (!m_decoder->DecodeNextFrame(false, true))
            break;

        if (clipPreviewActive && currentPts >= clipPreviewEndTime)
        {
            clipPreviewActive = false;
            Pause();
            UpdateControls();
            break;
        }

        double frameDur = frameRate > 0.0 ? 1.0 / frameRate : 0.0;
        int catchup = 0;
        while (playbackThreadRunning)
        {
            double elapsed = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - masterStartTime).count();
            double expected = masterStartPts + elapsed * playbackSpeed;
            if (currentPts + frameDur >= expected || catchup >= 8)
                break;
            if (!m_decoder->DecodeNextFrame(false, false))
            {
                playbackThreadRunning = false;
                break;
            }
            UpdateTimeline();
            ++catchup;
        }

        double target = (currentPts - masterStartPts) / playbackSpeed;
        double elapsed = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - masterStartTime).count();
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
        else if (msg == WM_LBUTTONDOWN)
        {
            SetFocus(hwnd);
            if (player->isLoaded)
            {
                player->selectingCrop = true;
                player->cropStart = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                player->cropCurrent = player->cropStart;
                SetCapture(hwnd);
            }
            return 0;
        }
        else if (msg == WM_MOUSEMOVE && player->selectingCrop)
        {
            player->cropCurrent = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        else if (msg == WM_LBUTTONUP && player->selectingCrop)
        {
            player->selectingCrop = false;
            ReleaseCapture();
            player->cropCurrent = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT winRect = { std::min(player->cropStart.x, player->cropCurrent.x),
                             std::min(player->cropStart.y, player->cropCurrent.y),
                             std::max(player->cropStart.x, player->cropCurrent.x),
                             std::max(player->cropStart.y, player->cropCurrent.y) };
            RECT client; GetClientRect(hwnd, &client);
            float wndW = (float)(client.right - client.left);
            float wndH = (float)(client.bottom - client.top);

            RECT base = player->hasCrop ? player->cropRect : RECT{0,0,player->frameWidth, player->frameHeight};
            float baseW = (float)(base.right - base.left);
            float baseH = (float)(base.bottom - base.top);

            float videoAspect = baseW / baseH;
            float targetAspect = wndW / wndH;
            float drawW, drawH, offsetX, offsetY;
            if (targetAspect > videoAspect) {
                drawH = wndH;
                drawW = drawH * videoAspect;
                offsetX = (wndW - drawW) / 2.0f;
                offsetY = 0.0f;
            } else {
                drawW = wndW;
                drawH = drawW / videoAspect;
                offsetX = 0.0f;
                offsetY = (wndH - drawH) / 2.0f;
            }
            float x1 = std::clamp((float)winRect.left - offsetX, 0.0f, drawW);
            float y1 = std::clamp((float)winRect.top - offsetY, 0.0f, drawH);
            float x2 = std::clamp((float)winRect.right - offsetX, 0.0f, drawW);
            float y2 = std::clamp((float)winRect.bottom - offsetY, 0.0f, drawH);
            if (x2 > x1 && y2 > y1) {
                RECT newRect;
                newRect.left = base.left + (LONG)(x1 / drawW * baseW);
                newRect.top = base.top + (LONG)(y1 / drawH * baseH);
                newRect.right = base.left + (LONG)(x2 / drawW * baseW);
                newRect.bottom = base.top + (LONG)(y2 / drawH * baseH);

                // Ensure cropping dimensions are even so the H.264 encoder can open
                auto make_even_floor = [](LONG v) { return v & ~1; };
                auto make_even_ceil  = [](LONG v) { return (v + 1) & ~1; };
                newRect.left = std::max<LONG>(base.left, make_even_floor(newRect.left));
                newRect.top = std::max<LONG>(base.top, make_even_floor(newRect.top));
                newRect.right = std::min<LONG>(base.right, make_even_ceil(newRect.right));
                newRect.bottom = std::min<LONG>(base.bottom, make_even_ceil(newRect.bottom));

                if (newRect.right > newRect.left &&
                    newRect.bottom > newRect.top) {
                    player->cropStack.push_back(newRect);
                    player->cropRect = newRect;
                    player->hasCrop = true;
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateControls();
            return 0;
        }
        else if (msg == WM_RBUTTONUP)
        {
            if (!player->cropStack.empty())
            {
                player->cropStack.pop_back();
                if (!player->cropStack.empty()) {
                    player->cropRect = player->cropStack.back();
                    player->hasCrop = true;
                } else {
                    player->cropRect = {0,0,0,0};
                    player->hasCrop = false;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateControls();
            }
            else
            {
                MessageBox(hwnd, L"Drag with the left mouse button to select a crop region. Right-click steps back one crop.", L"Crop", MB_OK);
            }
            return 0;
        }
        return CallWindowProc(player->originalVideoWndProc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}