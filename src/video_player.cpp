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
#include <cmath>
#include <iterator>
#include <cstring>
#include <chrono>

void UpdateControls();
void UpdateTimeline();

VideoPlayer::VideoPlayer(HWND parent)
    : parentWindow(parent), formatContext(nullptr), codecContext(nullptr),
      frame(nullptr), frameRGB(nullptr), hwFrame(nullptr), hwDeviceCtx(nullptr),
      hwPixelFormat(AV_PIX_FMT_NONE), useHwAccel(false), packet(nullptr), swsContext(nullptr),
      buffer(nullptr), rgbBufferSize(0), videoStreamIndex(-1), frameWidth(0), frameHeight(0),
      isLoaded(false), isPlaying(false), frameRate(0), currentFrame(0),
      totalFrames(0), currentPts(0.0), duration(0.0), startTimeOffset(0.0),
      clipPreviewActive(false), clipPreviewEndTime(0.0),
      cropRect{0,0,0,0}, cropTimeline(), hasCrop(false), cropOutputWidth(0), cropOutputHeight(0),
      selectingCrop(false), cropStart{0,0}, cropCurrent{0,0},
      videoWindow(nullptr),
      d2dFactory(nullptr), d2dRenderTarget(nullptr), d2dBitmap(nullptr), playbackTimer(0),
      deviceEnumerator(nullptr), audioDevice(nullptr), audioClient(nullptr),
      renderClient(nullptr), audioFormat(nullptr), bufferFrameCount(0),
      audioInitialized(false), audioOutputIsFloat(false), audioThreadRunning(false),
      playbackThreadRunning(false),
      audioSampleRate(44100), audioChannels(2), audioSampleFormat(AV_SAMPLE_FMT_S16),
    originalVideoWndProc(nullptr),
      dropAudioDuringStepping(false), frameCacheLimit(20)
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
    if (d2dBitmap)
    {
        d2dBitmap->Release();
        d2dBitmap = nullptr;
    }
    if (d2dRenderTarget)
    {
        d2dRenderTarget->BeginDraw();
        d2dRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        d2dRenderTarget->EndDraw();
    }
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
    frameWidth = 0;
    frameHeight = 0;
    ClearCropKeyframes();
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
                UpdateCropForTime(currentPts);
                m_renderer->UpdateDisplay();
                UpdateTimeline();
                return;
            }
        }
    }

    dropAudioDuringStepping = true;

    // Helper lambda to cache the current frame before stepping away from it
    auto cacheCurrentFrame = [this]() {
        if (buffer && rgbBufferSize > 0) {
            // Check if this frame is already in cache
            bool alreadyCached = false;
            for (const auto& cf : frameCache) {
                if (cf.number == currentFrame) {
                    alreadyCached = true;
                    break;
                }
            }
            if (!alreadyCached) {
                // Evict oldest if at limit
                if (frameCache.size() >= frameCacheLimit) {
                    frameCache.pop_front();
                }
                frameCache.push_back({
                    currentFrame,
                    currentPts,
                    std::vector<uint8_t>(buffer, buffer + rgbBufferSize)
                });
            }
        }
    };

    // Optimize stepping forward one frame by decoding without seeking
    if (frameNumber == currentFrame + 1)
    {
        // Cache current frame before moving forward (enables fast backward stepping)
        cacheCurrentFrame();
        
        m_decoder->DecodeNextFrame(true);
        // Ensure currentFrame stays in sync with PTS to prevent drift
        int64_t ptsFrame = static_cast<int64_t>(currentPts * frameRate + 0.5);
        currentFrame = ptsFrame;
        dropAudioDuringStepping = false;
        return;
    }
    
    // Optimize stepping backward one frame - cache current before seeking
    if (frameNumber == currentFrame - 1)
    {
        cacheCurrentFrame();
    }

    // For backwards navigation
    if (frameNumber < currentFrame)
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
            
            // Remove only frames before the target from cache, keep frames at or after target
            // This preserves cached frames we might step forward to again
            while (!frameCache.empty() && frameCache.front().number < frameNumber)
            {
                frameCache.pop_front();
            }
            
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
            
            // Update the current position estimation using index to be more accurate
            const AVIndexEntry *entry = avformat_index_get_entry_from_timestamp(vs, ts, AVSEEK_FLAG_BACKWARD);
            if (entry)
            {
                int64_t keyTs = entry->timestamp;
                double keyTime = keyTs * av_q2d(vs->time_base) - startTimeOffset;
                currentPts = keyTime;
                currentFrame = static_cast<int64_t>(currentPts * frameRate + 0.5);
            }
            else
            {
                currentPts = seekTime;
                currentFrame = static_cast<int64_t>(currentPts * frameRate + 0.5);
            }
            UpdateCropForTime(currentPts);
        }
        
        // Decode frames until we reach our target frame
        while (currentFrame <= frameNumber)
        {
            bool last = (currentFrame == frameNumber);
            
            // Cache frames preceding the target to enable instant step-back
            // But only if we are not too far away from target, to save memory and time
            bool shouldCache = (frameNumber - currentFrame) < static_cast<int64_t>(frameCacheLimit);

            if (!m_decoder->DecodeNextFrame(last, false, shouldCache))
                break;
                
            // Sync currentFrame to the actual decoded PTS to prevent drift
            // currentFrame tracks the next frame index (or count of decoded frames), so it should be FrameIndex + 1
            int64_t ptsFrame = static_cast<int64_t>(currentPts * frameRate + 0.5);
            currentFrame = ptsFrame;
        }
        
        // Ensure currentFrame is exactly the frame we targeted to maintain consistent 1-frame stepping
        currentFrame = frameNumber;
    }
    else
    {
        // For forward seeking beyond next frame
        double seconds = frameRate > 0 ? (frameNumber / frameRate) : 0.0;
        SeekToTime(seconds, 0);

        while (currentFrame < frameNumber)
        {
            bool last = (currentFrame + 1 >= frameNumber);
            if (!m_decoder->DecodeNextFrame(last, false, last))
                break;
        }
        
        // Ensure currentFrame is exactly the frame we targeted to prevent drift
        currentFrame = frameNumber;
    }

    dropAudioDuringStepping = false;
}

void VideoPlayer::SeekToTime(double seconds, int decodeCount)
{
    if (!isLoaded)
        return;

    // Clamp seconds to valid range
    if (seconds < 0.0) seconds = 0.0;
    if (duration > 0.0 && seconds >= duration) {
        double frameDur = (frameRate > 0) ? (1.0 / frameRate) : 0.033;
        seconds = duration - frameDur;
        if (seconds < 0.0) seconds = 0.0;
    }

    double frameDuration = (frameRate > 0) ? (1.0 / frameRate) : 0.033;
    bool smartSeek = false;

    // Check if we can just decode forward without seeking
    // This optimization is crucial for smooth timeline dragging
    {
        std::lock_guard<std::mutex> lock(decodeMutex);
        
        // If target is ahead of current position but within a reasonable range (e.g. 100 frames)
        // we can just decode forward instead of seeking back to the previous keyframe.
        double threshold = frameDuration * 100.0; 
        if (seconds >= currentPts && seconds <= currentPts + threshold)
        {
            smartSeek = true;
        }
    }

    if (!smartSeek)
    {
        std::lock_guard<std::mutex> lock(decodeMutex);
        frameCache.clear();

        AVStream *vs = formatContext->streams[videoStreamIndex];
        int64_t ts = (int64_t)((seconds + startTimeOffset) / av_q2d(vs->time_base));

        // Seek to previous keyframe (BACKWARD only, no ANY) to ensure we land on a valid reference frame
        av_seek_frame(formatContext, videoStreamIndex, ts, AVSEEK_FLAG_BACKWARD);
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

        // Update currentPts to the keyframe time
        const AVIndexEntry *entry = avformat_index_get_entry_from_timestamp(vs, ts, AVSEEK_FLAG_BACKWARD);
        if (entry)
        {
            int64_t keyTs = entry->timestamp;
            currentPts = keyTs * av_q2d(vs->time_base) - startTimeOffset;
            currentFrame = static_cast<int64_t>(currentPts * frameRate + 0.5);
        }
        else
        {
            // Fallback: assume we are before the target so decoding can proceed
            currentPts = -1.0;
            currentFrame = 0;
        }
        UpdateCropForTime(currentPts);
    }

    // Decode frames until we reach the target time
    int maxDecodeFrames = 1000; // Safety limit
    int decoded = 0;
    
    // After a full seek (non-smartSeek), we must decode at least one frame
    // because the seek only positions the decoder - it doesn't decode a frame yet
    bool needAtLeastOneFrame = !smartSeek;
    
    while (decoded < maxDecodeFrames)
    {
        // Check if we reached target (within half a frame tolerance)
        // But always decode at least one frame after a full seek
        if (!needAtLeastOneFrame && currentPts >= seconds - (frameDuration * 0.5))
            break;
            
        // Optimization: only generate image if we are close to the target
        bool closeToTarget = (seconds - currentPts) < 0.2 || (seconds - currentPts) < (frameDuration * 5.0);

        // Decode next frame without presenting it yet
        if (!m_decoder->DecodeNextFrame(false, false, closeToTarget))
            break;
            
        decoded++;
        needAtLeastOneFrame = false;  // We've decoded at least one frame now
    }
    
    // Ensure the final frame is displayed
    m_renderer->UpdateDisplay();
    UpdateTimeline();
}

void VideoPlayer::SeekToTimeExact(double seconds)
{
    if (!isLoaded)
        return;

    // Seek to the target time
    SeekToTime(seconds, 0);
    
    // After seeking and decoding a frame, the actual PTS might differ from the target.
    // For keyframe editing, we want to be exactly at the keyframe time.
    // Lock and directly set currentPts to the requested time to ensure exact positioning.
    {
        std::lock_guard<std::mutex> lock(decodeMutex);
        currentPts = seconds;
        currentFrame = frameRate > 0 ? static_cast<int64_t>(seconds * frameRate) : 0;
        UpdateCropForTime(currentPts);
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

namespace
{
    constexpr double kCropTimeEpsilon = 0.02;
    constexpr LONG kFullFrameTolerance = 2;

    inline bool RectEquals(const RECT& a, const RECT& b)
    {
        return a.left == b.left && a.top == b.top &&
               a.right == b.right && a.bottom == b.bottom;
    }

    inline bool KeyframeHasCrop(const VideoPlayer::CropKeyframe& key,
                                int frameWidth, int frameHeight)
    {
        if (!key.enabled)
            return false;

        LONG width = key.rect.right - key.rect.left;
        LONG height = key.rect.bottom - key.rect.top;

        if (width >= static_cast<LONG>(frameWidth) - kFullFrameTolerance &&
            height >= static_cast<LONG>(frameHeight) - kFullFrameTolerance)
            return false;

        return width > 0 && height > 0 &&
               (width < frameWidth || height < frameHeight);
    }

    inline LONG MakeEvenFloor(LONG value)
    {
        return value & ~1L;
    }

    inline LONG MakeEvenCeil(LONG value)
    {
        return (value + 1L) & ~1L;
    }
}

void VideoPlayer::ClearCropKeyframes()
{
    std::lock_guard<std::mutex> lock(cropMutex);
    cropTimeline.clear();
    RecomputeCropOutputDimensionsLocked();
    cropRect = {0, 0, 0, 0};
    hasCrop = false;
}

bool VideoPlayer::HasAnyCrop() const
{
    std::lock_guard<std::mutex> lock(cropMutex);
    if (frameWidth <= 0 || frameHeight <= 0)
        return false;
    for (const auto& key : cropTimeline)
    {
        if (KeyframeHasCrop(key, frameWidth, frameHeight))
            return true;
    }
    return false;
}

std::vector<double> VideoPlayer::GetCropKeyframeTimes() const
{
    std::lock_guard<std::mutex> lock(cropMutex);
    std::vector<double> times;
    times.reserve(cropTimeline.size());
    for (const auto& key : cropTimeline)
    {
        if (!key.enabled)
            continue;
        LONG width = key.rect.right - key.rect.left;
        LONG height = key.rect.bottom - key.rect.top;
        if (width > 0 && height > 0)
            times.push_back(key.time);
    }
    return times;
}

std::vector<VideoPlayer::CropKeyframe> VideoPlayer::GetCropKeyframes() const
{
    std::lock_guard<std::mutex> lock(cropMutex);
    return cropTimeline;
}

void VideoPlayer::RecomputeCropOutputDimensionsLocked()
{
    if (frameWidth <= 0 || frameHeight <= 0)
    {
        cropOutputWidth = 0;
        cropOutputHeight = 0;
        return;
    }

    // Always preserve the original canvas size for export. Zooming should
    // enlarge the selected region rather than shrinking the output video.
    cropOutputWidth = frameWidth;
    cropOutputHeight = frameHeight;
}

bool VideoPlayer::GetCropRectForTime(double time, RECT &outRect) const
{
    std::lock_guard<std::mutex> lock(cropMutex);

    double clampedTime = time;
    if (duration > 0.0)
        clampedTime = std::clamp(clampedTime, 0.0, duration);

    // Find the keyframe that applies at this time (the one at or before this time)
    const CropKeyframe* result = nullptr;
    for (const auto& key : cropTimeline)
    {
        if (key.time <= clampedTime)
            result = &key;
        else
            break;
    }

    if (!result || !result->enabled)
        return false;

    outRect = result->rect;
    LONG width = outRect.right - outRect.left;
    LONG height = outRect.bottom - outRect.top;
    return width > 0 && height > 0 &&
           (width < frameWidth || height < frameHeight);
}

bool VideoPlayer::UpdateCropForTime(double time)
{
    RECT rect;
    bool active = GetCropRectForTime(time, rect);

    if (!active)
    {
        RECT empty{0, 0, 0, 0};
        bool changed = hasCrop || !RectEquals(cropRect, empty);
        cropRect = empty;
        hasCrop = false;
        return changed;
    }

    if (!hasCrop || !RectEquals(cropRect, rect))
    {
        cropRect = rect;
        hasCrop = true;
        return true;
    }
    return false;
}

bool VideoPlayer::AddCropKeyframe(double time, RECT rect, double* actualTime)
{
    if (!isLoaded || frameWidth <= 0 || frameHeight <= 0)
        return false;

    RECT normalized = rect;
    if (normalized.left > normalized.right)
        std::swap(normalized.left, normalized.right);
    if (normalized.top > normalized.bottom)
        std::swap(normalized.top, normalized.bottom);

    normalized.left = std::clamp<LONG>(normalized.left, 0, static_cast<LONG>(frameWidth));
    normalized.top = std::clamp<LONG>(normalized.top, 0, static_cast<LONG>(frameHeight));
    normalized.right = std::clamp<LONG>(normalized.right, 0, static_cast<LONG>(frameWidth));
    normalized.bottom = std::clamp<LONG>(normalized.bottom, 0, static_cast<LONG>(frameHeight));

    double clampedTime = time;
    if (duration > 0.0)
        clampedTime = std::clamp(clampedTime, 0.0, duration);

    std::lock_guard<std::mutex> lock(cropMutex);

    normalized.left = MakeEvenFloor(normalized.left);
    normalized.top = MakeEvenFloor(normalized.top);
    normalized.right = MakeEvenCeil(normalized.right);
    normalized.bottom = MakeEvenCeil(normalized.bottom);

    if (normalized.left < 0)
        normalized.left = 0;
    if (normalized.top < 0)
        normalized.top = 0;
    LONG maxRight = MakeEvenFloor(static_cast<LONG>(frameWidth));
    LONG maxBottom = MakeEvenFloor(static_cast<LONG>(frameHeight));

    if (normalized.right > frameWidth)
        normalized.right = maxRight;
    if (normalized.bottom > frameHeight)
        normalized.bottom = maxBottom;

    if (normalized.right <= normalized.left + 1 ||
        normalized.bottom <= normalized.top + 1)
        return false;

    LONG width = normalized.right - normalized.left;
    LONG height = normalized.bottom - normalized.top;

    if (width % 2 != 0)
    {
        --width;
        normalized.right = normalized.left + width;
    }
    if (height % 2 != 0)
    {
        --height;
        normalized.bottom = normalized.top + height;
    }

    bool isFullFrame = (normalized.left <= kFullFrameTolerance && normalized.top <= kFullFrameTolerance &&
                        normalized.right >= maxRight - kFullFrameTolerance &&
                        normalized.bottom >= maxBottom - kFullFrameTolerance);

    auto it = std::lower_bound(
        cropTimeline.begin(), cropTimeline.end(), clampedTime,
        [](const CropKeyframe& entry, double value)
        {
            return entry.time < value;
        });

    if (isFullFrame)
    {
        CropKeyframe disabled{ clampedTime, RECT{0, 0, 0, 0}, false };
        // Only replace keyframe if it's at essentially the exact same time (within 1ms)
        // This prevents nearby keyframes from being overwritten
        if (it != cropTimeline.end() && std::fabs(it->time - clampedTime) < 0.001)
            *it = disabled;
        else
            cropTimeline.insert(it, disabled);

        RecomputeCropOutputDimensionsLocked();
        if (actualTime)
            *actualTime = clampedTime;
        return true;
    }

    CropKeyframe key{ clampedTime, normalized, true };

    // Only replace keyframe if it's at essentially the exact same time (within 1ms)
    // This prevents nearby keyframes from being overwritten
    if (it != cropTimeline.end() && std::fabs(it->time - clampedTime) < 0.001)
        *it = key;
    else
        cropTimeline.insert(it, key);

    RecomputeCropOutputDimensionsLocked();
    if (actualTime)
        *actualTime = clampedTime;
    return true;
}

bool VideoPlayer::AddCropDisabledKeyframe(double time, double* actualTime)
{
    if (!isLoaded)
        return false;

    double clampedTime = time;
    if (duration > 0.0)
        clampedTime = std::clamp(clampedTime, 0.0, duration);

    std::lock_guard<std::mutex> lock(cropMutex);

    CropKeyframe key{ clampedTime, RECT{0, 0, 0, 0}, false };
    auto it = std::lower_bound(
        cropTimeline.begin(), cropTimeline.end(), clampedTime,
        [](const CropKeyframe& entry, double value)
        {
            return entry.time < value;
        });

    // Only replace keyframe if it's at essentially the exact same time (within 1ms)
    // This prevents nearby keyframes from being overwritten
    if (it != cropTimeline.end() && std::fabs(it->time - clampedTime) < 0.001)
        *it = key;
    else
        cropTimeline.insert(it, key);

    RecomputeCropOutputDimensionsLocked();
    if (actualTime)
        *actualTime = clampedTime;
    return true;
}

bool VideoPlayer::RemoveCropKeyframe(double time)
{
    std::lock_guard<std::mutex> lock(cropMutex);
    if (cropTimeline.empty())
        return false;

    double clampedTime = time;
    if (duration > 0.0)
        clampedTime = std::clamp(clampedTime, 0.0, duration);

    // Find the keyframe that's closest to the requested time (within 1ms tolerance for exact match)
    // This prevents removing the wrong keyframe when multiple keyframes exist nearby
    auto best = cropTimeline.end();
    double closestDistance = 1.0;  // Initialize to large value
    
    for (auto it = cropTimeline.begin(); it != cropTimeline.end(); ++it)
    {
        double distance = std::fabs(it->time - clampedTime);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            best = it;
        }
    }

    if (best == cropTimeline.end())
        return false;

    cropTimeline.erase(best);
    RecomputeCropOutputDimensionsLocked();
    return true;
}

bool VideoPlayer::MoveCropKeyframe(double oldTime, double newTime)
{
    if (!isLoaded || duration <= 0.0)
        return false;

    // Clamp times to valid range
    double clampedOldTime = std::clamp(oldTime, 0.0, duration);
    double clampedNewTime = std::clamp(newTime, 0.0, duration);

    // Don't move to the same time
    if (std::fabs(clampedOldTime - clampedNewTime) < 0.001)
        return false;

    std::lock_guard<std::mutex> lock(cropMutex);

    // Find the keyframe at the old time
    auto keyframeIt = cropTimeline.end();
    double closestDistance = 1.0;
    
    for (auto it = cropTimeline.begin(); it != cropTimeline.end(); ++it)
    {
        double distance = std::fabs(it->time - clampedOldTime);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            keyframeIt = it;
        }
    }

    if (keyframeIt == cropTimeline.end())
        return false;

    // Store the keyframe data
    CropKeyframe keyframe = *keyframeIt;
    keyframe.time = clampedNewTime;

    // Remove the old keyframe
    cropTimeline.erase(keyframeIt);

    // Insert at the new time (maintaining sorted order)
    auto insertIt = std::lower_bound(
        cropTimeline.begin(), cropTimeline.end(), clampedNewTime,
        [](const CropKeyframe& entry, double value)
        {
            return entry.time < value;
        });

    cropTimeline.insert(insertIt, keyframe);
    RecomputeCropOutputDimensionsLocked();
    return true;
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
    constexpr float kMinAudibleAtMinus30Db = 0.03162278f; // 10^(-30/20)
    float clampedVolume = volume < 0.0f ? 0.0f : volume;
    if (clampedVolume > 0.0f && clampedVolume < kMinAudibleAtMinus30Db)
        clampedVolume = 0.0f;
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
    // Synchronize with audio processing to avoid freeing resources in use
    std::lock_guard<std::mutex> lock(audioMutex);

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
        // Disable first so processing threads skip the path, then free resources
        track->voiceIsolationEnabled = false;
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
        track->voiceIsolationInputBuffer.clear();
        track->voiceIsolationMonoBuffer.clear();
        track->voiceIsolationProcessedBuffer.clear();
        track->voiceIsolationSampleQueue.clear();
    }
}

void VideoPlayer::PlaybackThreadFunction()
{
    auto startTime = masterStartTime;
    double startPts = masterStartPts;
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
                           EncoderSelection encoder, const std::wstring& qualityPreset, int maxBitrate, HWND progressBar,
                           std::atomic<bool>* cancelFlag)
{
    return m_cutter->CutVideo(outputFilename, startTime, endTime, mergeAudio, convertH264, encoder, qualityPreset, maxBitrate, progressBar, cancelFlag);
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

            RECT base;
            if (player->hasCrop)
                base = player->cropRect;
            else
            {
                base.left = 0;
                base.top = 0;
                base.right = player->frameWidth;
                base.bottom = player->frameHeight;
            }
            float baseW = (float)(base.right - base.left);
            float baseH = (float)(base.bottom - base.top);

            if (baseW <= 0.0f || baseH <= 0.0f)
            {
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateControls();
                player->UpdateCropForTime(player->GetCurrentTime());
                UpdateTimeline();
                return 0;
            }

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

                if (newRect.right > newRect.left &&
                    newRect.bottom > newRect.top) {
                    double selectionTime = player->GetCurrentTime();
                    if (player->duration > 0.0)
                        selectionTime = std::clamp(selectionTime, 0.0, player->duration);
                    double appliedTime = selectionTime;
                    bool inserted = player->AddCropKeyframe(selectionTime, newRect, &appliedTime);
                    player->UpdateCropForTime(inserted ? appliedTime : selectionTime);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateControls();
            UpdateTimeline();
            return 0;
        }
        else if (msg == WM_RBUTTONUP)
        {
            if (!player->isLoaded)
                return 0;

            double currentTime = player->GetCurrentTime();
            double clampedTime = currentTime;
            if (player->duration > 0.0)
                clampedTime = std::clamp(clampedTime, 0.0, player->duration);

            auto keyframes = player->GetCropKeyframes();
            bool hasCurrent = false;
            int currentKeyframeIndex = -1;

            // Find the keyframe that's currently applying at this playback time
            // (the one at or before the current time)
            for (int i = static_cast<int>(keyframes.size()) - 1; i >= 0; --i)
            {
                if (keyframes[i].time <= clampedTime)
                {
                    hasCurrent = true;
                    currentKeyframeIndex = i;
                    break;
                }
            }

            if (!hasCurrent)
            {
                MessageBox(hwnd, L"Drag with the left mouse button to select a crop region. Right-click steps back one crop.", L"Crop", MB_OK);
                return 0;
            }

            // If there's a keyframe at the current time, we can do hierarchical undo
            // Otherwise, just add a disabled keyframe at the current location
            bool foundPreviousEnabled = false;
            if (currentKeyframeIndex >= 0 && std::fabs(keyframes[currentKeyframeIndex].time - clampedTime) < 0.001)
            {
                // Only do hierarchical undo if the current keyframe is AT this location
                if (currentKeyframeIndex > 0)
                {
                    // Check if there's a previous enabled keyframe
                    for (int i = currentKeyframeIndex - 1; i >= 0; --i)
                    {
                        if (keyframes[i].enabled)
                        {
                            // Found a previous enabled keyframe, remove the current one to restore to that state
                            player->RemoveCropKeyframe(keyframes[currentKeyframeIndex].time);
                            foundPreviousEnabled = true;
                            break;
                        }
                    }
                }
                
                // If no previous enabled keyframe but we're on a keyframe, remove it to go to full frame
                if (!foundPreviousEnabled)
                {
                    player->RemoveCropKeyframe(keyframes[currentKeyframeIndex].time);
                    foundPreviousEnabled = true;
                }
            }

            // If we didn't find a keyframe at current location, or undo didn't happen, add disabled keyframe
            if (!foundPreviousEnabled)
            {
                double appliedTime = clampedTime;
                player->AddCropDisabledKeyframe(clampedTime, &appliedTime);
            }

            player->UpdateCropForTime(clampedTime);
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateControls();
            UpdateTimeline();
            return 0;
        }
        return CallWindowProc(player->originalVideoWndProc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
