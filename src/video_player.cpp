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
#include <climits>
#include <cerrno>

void UpdateControls();
void UpdateTimeline();

namespace
{
constexpr UINT WM_BWD_FRAME_READY = WM_APP + 20;
}

VideoPlayer::VideoPlayer(HWND parent)
    : parentWindow(parent), formatContext(nullptr), codecContext(nullptr),
      frame(nullptr), frameRGB(nullptr), hwFrame(nullptr), hwDeviceCtx(nullptr),
      hwPixelFormat(AV_PIX_FMT_NONE), useHwAccel(false), packet(nullptr), swsContext(nullptr),
      swsSourceFormat(AV_PIX_FMT_NONE),
      buffer(nullptr), rgbBufferSize(0), playbackSwsContext(nullptr),
      playbackSwsSourceFormat(AV_PIX_FMT_NONE), playbackRgbWidth(0), playbackRgbHeight(0),
      playbackRgbStride(0), displayUsesPlaybackBuffer(false), videoStreamIndex(-1),
      frameWidth(0), frameHeight(0),
      isLoaded(false), isPlaying(false), frameRate(0), currentFrame(0),
      totalFrames(0), currentPts(0.0), duration(0.0), startTimeOffset(0.0),
      clipPreviewActive(false), clipPreviewEndTime(0.0),
      cropRect{0,0,0,0}, cropTimeline(), hasCrop(false), cropOutputWidth(0), cropOutputHeight(0),
      selectingCrop(false), cropStart{0,0}, cropCurrent{0,0},
      videoWindow(nullptr),
      d2dFactory(nullptr), d2dRenderTarget(nullptr), d2dBitmap(nullptr),
      dwriteFactory(nullptr), speedTextFormat(nullptr), playbackTimer(0),
      deviceEnumerator(nullptr), audioDevice(nullptr), audioClient(nullptr),
      renderClient(nullptr), audioFormat(nullptr), bufferFrameCount(0),
      audioInitialized(false), audioOutputIsFloat(false), audioThreadRunning(false),
      playbackThreadRunning(false),
      playbackBufferCapacity(3), playbackPrebufferFrames(2), playbackDecodeEof(false),
      audioSampleRate(44100), audioChannels(2), audioSampleFormat(AV_SAMPLE_FMT_S16),
    originalVideoWndProc(nullptr),
            dropAudioDuringStepping(false),
            m_decoderOutOfSync(false),
            m_playbackSeekPending(false),
            m_playbackSeekInProgress(false),
            m_playbackSeekGeneration(0),
            m_playbackSeekTarget(0.0),
            m_playbackSeekExact(true),
            m_resumeSeekPending(false),
            m_resumeSeekTarget(0.0),
            m_playbackSpeed(1.0),
            m_playbackSpeedChangePending(false),
            m_playbackClockStartPts(0.0),
            m_playbackClockStartNs(0),
            m_presentedPlaybackFrameCount(0),
            m_lastHighSpeedFrameDeliveryNs(0),
            m_speedOverlayDeadline(0),
            seekRefineThreadExit(false), seekRefinePending(false), seekRefineTarget(0.0),
            seekRefineGeneration(0)
{
    m_decoder = std::make_unique<VideoDecoder>(this);
    m_audioPlayer = std::make_unique<AudioPlayer>(this);
    m_renderer = std::make_unique<VideoRenderer>(this);
    m_cutter = std::make_unique<VideoCutter>(this);

    m_bwdPrefetch = std::make_unique<BwdPrefetch>();
    m_bwdPrefetch->thread = std::thread(&VideoPlayer::BwdPrefetchThreadFunc, this);

        seekRefineThread = std::thread(&VideoPlayer::SeekRefinementThreadFunction, this);

    m_renderer->Initialize();
    CreateVideoWindow();
    m_audioPlayer->Initialize();
}

VideoPlayer::~VideoPlayer()
{
    // Stop backward prefetch thread first
    {
        std::lock_guard<std::mutex> lk(m_bwdPrefetch->mtx);
        m_bwdPrefetch->exitFlag = true;
    }
    m_bwdPrefetch->cv.notify_one();
    if (m_bwdPrefetch->thread.joinable())
        m_bwdPrefetch->thread.join();

    CancelPendingSeekRefinement();
    {
        std::lock_guard<std::mutex> lock(decodeMutex);
    }
    {
        std::lock_guard<std::mutex> lock(seekRefineMutex);
        seekRefineThreadExit = true;
        seekRefinePending = false;
    }
    seekRefineCondition.notify_all();
    if (seekRefineThread.joinable())
        seekRefineThread.join();

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
    InitThumbnailCtx();
    // Tell the prefetch thread about the new file
    {
        std::lock_guard<std::mutex> lk(m_bwdPrefetch->mtx);
        m_bwdPrefetch->fileUtf8  = utf8Filename;
        m_bwdPrefetch->startOff  = startTimeOffset;
        m_bwdPrefetch->fps       = frameRate;
        m_bwdPrefetch->sw        = frameWidth;
        m_bwdPrefetch->sh        = frameHeight;
        m_bwdPrefetch->targetFrame = -1;
        m_bwdPrefetch->suspended = false;
        m_bwdPrefetch->stepTargetFrame = -1;
        m_bwdPrefetch->stepDirection = 0;
        m_bwdPrefetch->cache.clear();
        m_bwdPrefetch->workGen++;
    }
    m_bwdPrefetch->cv.notify_one();
    return true;
}

void VideoPlayer::UnloadVideo()
{
    CleanupThumbnailCtx();
    Stop();
    CancelPendingSeekRefinement();
    {
        std::lock_guard<std::mutex> lock(decodeMutex);
    }
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
    // Signal prefetch thread that no file is open
    {
        std::lock_guard<std::mutex> lk(m_bwdPrefetch->mtx);
        m_bwdPrefetch->fileUtf8.clear();
        m_bwdPrefetch->targetFrame = -1;
        m_bwdPrefetch->suspended = false;
        m_bwdPrefetch->stepTargetFrame = -1;
        m_bwdPrefetch->stepDirection = 0;
        m_bwdPrefetch->cache.clear();
        m_bwdPrefetch->workGen++;
    }
    m_bwdPrefetch->cv.notify_one();
    isLoaded = false;
    videoStreamIndex = -1;
    currentFrame = 0;
    currentPts = 0.0;
    totalFrames = 0;
    duration = 0.0;
    frameWidth = 0;
    frameHeight = 0;
    ClearCropKeyframes();
}

bool VideoPlayer::Play()
{
    if (!isLoaded || isPlaying)
        return false;
    CancelPendingSeekRefinement();

    const bool hasInterruptedSeek =
        m_resumeSeekPending.exchange(false, std::memory_order_acq_rel);
    const double interruptedSeekTarget =
        m_resumeSeekTarget.load(std::memory_order_acquire);

    // If backward frame stepping used the prefetch cache, the main formatContext
    // was never seeked.  Resync it now so the playback thread decodes from the
    // correct position instead of the old pre-step position.
    if (m_decoderOutOfSync && !hasInterruptedSeek)
    {
        // hardSeek=true bypasses the smartSeek optimisation which would skip
        // av_seek_frame when seconds==currentPts, leaving the formatContext at
        // the wrong position when we consumed a prefetch-cache frame.
        if (!SeekToTimeInternal(currentPts, INT_MAX, false, true, true) &&
            !SeekToTimeInternal(currentPts, INT_MAX, false, true, true,
                                true, true, true))
            return false;
        // m_decoderOutOfSync is cleared inside SeekToTimeInternal
    }

    isPlaying = true;

    masterStartPts = currentPts;
    masterStartTime = std::chrono::high_resolution_clock::now();
    m_playbackClockStartPts.store(currentPts, std::memory_order_relaxed);
    m_playbackClockStartNs.store(0, std::memory_order_release);
    m_lastHighSpeedFrameDeliveryNs = 0;

    // Keep roughly 350 ms of decoded video ready, while capping worst-case
    // memory usage (4 bytes/pixel is a conservative estimate for CPU frames).
    const size_t desiredFrames = static_cast<size_t>(std::clamp(
        static_cast<int>(std::ceil(frameRate * 0.35)), 3, 24));
    const size_t bytesPerFrame = std::max<size_t>(
        1, static_cast<size_t>(frameWidth) * static_cast<size_t>(frameHeight) * 4);
    const size_t memoryLimitedFrames = std::max<size_t>(3, (128ull * 1024ull * 1024ull) / bytesPerFrame);
    playbackBufferCapacity = std::min(desiredFrames, memoryLimitedFrames);
#ifdef VIDEO_EDITOR_TESTING
    if (m_testPlaybackBufferCapacity > 0)
        playbackBufferCapacity = m_testPlaybackBufferCapacity;
#endif
    playbackPrebufferFrames = std::min(playbackBufferCapacity, std::max<size_t>(2, playbackBufferCapacity / 2));
    if (GetPlaybackSpeed() >= 4.0)
        playbackPrebufferFrames = 1;
    ClearPlaybackBuffer();

    if (hasInterruptedSeek)
    {
        // Publish the interrupted target before either worker starts. The
        // presentation thread therefore cannot consume a frame from the old
        // decoder position, and the decode thread performs the exact recovery
        // off the UI thread.
        std::lock_guard<std::mutex> wakeLock(playbackWakeMutex);
        m_playbackSeekTarget.store(interruptedSeekTarget,
                                   std::memory_order_relaxed);
        m_playbackSeekExact.store(true, std::memory_order_relaxed);
        m_playbackSeekPending.store(true, std::memory_order_release);
        m_playbackSeekGeneration.fetch_add(1, std::memory_order_release);
    }
    playbackThreadRunning = true;
    playbackDecodeThread = std::thread(&VideoPlayer::PlaybackDecodeThreadFunction, this);
    playbackThread = std::thread(&VideoPlayer::PlaybackThreadFunction, this);
    playbackWakeCondition.notify_all();
    playbackBufferCondition.notify_all();
    return true;
}

void VideoPlayer::Pause()
{
    if (isPlaying)
    {
        const bool interruptedSeek =
            m_playbackSeekPending.load(std::memory_order_acquire) ||
            m_playbackSeekInProgress.load(std::memory_order_acquire);
        if (interruptedSeek)
        {
            std::lock_guard<std::mutex> wakeLock(playbackWakeMutex);
            m_resumeSeekTarget.store(
                m_playbackSeekTarget.load(std::memory_order_relaxed),
                std::memory_order_release);
            m_resumeSeekPending.store(true, std::memory_order_release);
        }

        isPlaying = false;
        clipPreviewActive = false;
        m_playbackSeekPending.store(false, std::memory_order_release);

        if (playbackThreadRunning)
        {
            playbackThreadRunning = false;
            playbackWakeCondition.notify_all();
            playbackBufferCondition.notify_all();
            if (playbackDecodeThread.joinable())
            {
                if (std::this_thread::get_id() == playbackDecodeThread.get_id())
                    playbackDecodeThread.detach();
                else
                    playbackDecodeThread.join();
            }
            if (playbackThread.joinable())
            {
                if (std::this_thread::get_id() == playbackThread.get_id())
                    playbackThread.detach();
                else
                    playbackThread.join();
            }
        }

        // A packet may be intentionally retained when avcodec_send_packet
        // reports EAGAIN. Playback is now stopped, so discard that pending
        // ownership before any paused seek or frame-step uses the decoder.
        m_decoder->ResetBufferedDecodeState();

        // High-speed playback temporarily enables decoder discard options.
        // Restore full-quality decoding before paused seeks/frame stepping.
        if (codecContext)
        {
            codecContext->skip_frame = AVDISCARD_DEFAULT;
            codecContext->skip_idct = AVDISCARD_DEFAULT;
            codecContext->skip_loop_filter = AVDISCARD_DEFAULT;
        }

        // The decode thread is positioned after every frame it prefetched,
        // while currentPts identifies the last frame actually presented.
        // Force Play() to realign those positions before it discards the queue.
        m_decoderOutOfSync = true;

        // The presentation thread starts audio after the video buffer has
        // prefilled. Stop it only after both playback threads are shut down so
        // it cannot race this pause and launch a new audio thread behind us.
        // Leaving the old std::thread joinable makes the next Play() terminate
        // the process when AudioPlayer::StartThread assigns its replacement.
        m_audioPlayer->StopThread();

        // When paused, pre-decode the previous frame so the first ',' is instant
        RequestBwdPrefetch(currentFrame - 1);
    }
}

void VideoPlayer::Stop()
{
    clipPreviewActive = false;
    Pause();
    m_resumeSeekPending.store(false, std::memory_order_release);
    ClearPlaybackBuffer();
    CancelPendingSeekRefinement();
    currentFrame = 0;
    currentPts = 0.0;
    if (isLoaded)
    {
        m_decoder->ResetBufferedDecodeState();
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
    m_decoderOutOfSync = false;
}

std::uint64_t VideoPlayer::BeginSeekOperation()
{
    {
        std::lock_guard<std::mutex> lock(m_bwdPrefetch->mtx);
        m_bwdPrefetch->stepTargetFrame = -1;
        m_bwdPrefetch->stepDirection = 0;
    }

    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(seekRefineMutex);
        generation = ++seekRefineGeneration;
        seekRefinePending = false;
    }
    seekRefineCondition.notify_all();
    return generation;
}

void VideoPlayer::QueueSeekRefinement(double seconds, std::uint64_t generation)
{
    {
        std::lock_guard<std::mutex> lock(seekRefineMutex);
        seekRefineTarget = seconds;
        seekRefineGeneration = generation;
        seekRefinePending = true;
    }
    seekRefineCondition.notify_one();
}

void VideoPlayer::CancelPendingSeekRefinement()
{
    BeginSeekOperation();
}

void VideoPlayer::SeekRefinementThreadFunction()
{
    while (true)
    {
        double target = 0.0;
        std::uint64_t generation = 0;

        {
            std::unique_lock<std::mutex> lock(seekRefineMutex);
            seekRefineCondition.wait(lock, [this]() {
                return seekRefineThreadExit || seekRefinePending;
            });

            if (seekRefineThreadExit)
                return;

            target = seekRefineTarget;
            generation = seekRefineGeneration;
            seekRefinePending = false;
        }

        if (!isLoaded || isPlaying)
            continue;

        const double frameDuration = (frameRate > 0.0) ? (1.0 / frameRate) : 0.033;
        while (isLoaded && !isPlaying)
        {
            {
                std::lock_guard<std::mutex> lock(seekRefineMutex);
                if (generation != seekRefineGeneration)
                    break;
            }

            if (currentPts >= target - (frameDuration * 0.5))
                break;

            double delta = target - currentPts;
            bool closeToTarget = delta < 0.2 || delta < (frameDuration * 5.0);
            
            if (!closeToTarget)
                codecContext->skip_frame = AVDISCARD_NONREF;
            else
                codecContext->skip_frame = AVDISCARD_DEFAULT;

            dropAudioDuringStepping = true;
            bool decodeSuccess = m_decoder->DecodeNextFrame(false, false, closeToTarget);
            dropAudioDuringStepping = false;
            
            if (!decodeSuccess)
                break;
        }
        codecContext->skip_frame = AVDISCARD_DEFAULT;

        bool shouldPresent = false;
        {
            std::lock_guard<std::mutex> lock(seekRefineMutex);
            shouldPresent = isLoaded && !isPlaying && generation == seekRefineGeneration &&
                            currentPts >= target - (frameDuration * 0.5);
        }

        if (shouldPresent)
        {
            m_renderer->UpdateDisplay();
            UpdateTimeline();
        }
    }
}

void VideoPlayer::PlayClip(double startTime, double endTime)
{
    if (!isLoaded)
        return;
    if (isPlaying)
        Pause();

    clipPreviewSegments.clear();
    clipPreviewSegmentIndex = 0;
    clipPreviewEndTime = endTime;
    clipPreviewActive = true;
    SeekToTimeExact(startTime);
    Play();
}

void VideoPlayer::PlayClips(const std::vector<ClipSegment>& segments)
{
    if (!isLoaded)
        return;

    std::vector<ClipSegment> validSegments;
    for (auto segment : segments) {
        segment.start = std::clamp(segment.start, 0.0, duration);
        segment.end = std::clamp(segment.end, 0.0, duration);
        if (segment.end > segment.start)
            validSegments.push_back(segment);
    }
    std::sort(validSegments.begin(), validSegments.end(), [](const ClipSegment& a, const ClipSegment& b) {
        return a.start < b.start;
    });
    if (validSegments.empty())
        return;

    if (isPlaying)
        Pause();

    clipPreviewSegments = std::move(validSegments);
    clipPreviewSegmentIndex = 0;
    clipPreviewEndTime = clipPreviewSegments.front().end;
    clipPreviewActive = true;
    SeekToTimeExact(clipPreviewSegments.front().start);
    Play();
}

bool VideoPlayer::AdvanceClipPreview()
{
    if (!clipPreviewActive || clipPreviewSegments.empty() ||
        clipPreviewSegmentIndex + 1 >= clipPreviewSegments.size())
        return false;

    ++clipPreviewSegmentIndex;
    clipPreviewEndTime = clipPreviewSegments[clipPreviewSegmentIndex].end;
    SeekWhilePlaying(clipPreviewSegments[clipPreviewSegmentIndex].start, true);
    return true;
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

    CancelPendingSeekRefinement();
    dropAudioDuringStepping = true;

    // The ordinary next-frame path must remain independent from the reverse
    // cache. It was historically stall-free and keeps the main decoder aligned.
    if (frameNumber == currentFrame + 1 && !m_decoderOutOfSync)
    {
        SuspendBwdPrefetch();
        m_decoder->DecodeNextFrame(true);
        currentFrame = static_cast<int64_t>(currentPts * frameRate + 0.5);
        dropAudioDuringStepping = false;
        return;
    }

    // The background window contains frames on both sides of the current
    // position. Use it before deciding whether the decoder needs a seek. This
    // makes direction changes (',' followed by '.') instantaneous as well.
    extern bool g_improveSeekPerformance;
    const bool steppingBackward = frameNumber < currentFrame;
    if (g_improveSeekPerformance && ConsumeBwdPrefetch(frameNumber, steppingBackward))
    {
        dropAudioDuringStepping = false;
        RequestBwdPrefetch(frameNumber - 1);
        return;
    }

    // Backward navigation
    if (frameNumber < currentFrame)
    {
        // A cache miss must still move one frame. The previous implementation
        // only queued prefetch work and returned, making the button appear
        // stuck until the background decoder happened to catch up.
        double seconds = frameRate > 0 ? (frameNumber / frameRate) : 0.0;
        SeekToTimeInternal(seconds, INT_MAX, false, true);
        while (currentFrame < frameNumber) {
            bool last = (currentFrame + 1 >= frameNumber);
            if (!m_decoder->DecodeNextFrame(last, false, last)) break;
            currentFrame = static_cast<int64_t>(currentPts * frameRate + 0.5);
        }
        currentFrame = frameNumber;
        dropAudioDuringStepping = false;
        RequestBwdPrefetch(currentFrame - 1);
        return;
    }
    else
    {
        double seconds = frameRate > 0 ? (frameNumber / frameRate) : 0.0;
        SeekToTimeInternal(seconds, INT_MAX, false, true);
        while (currentFrame < frameNumber) {
            bool last = (currentFrame + 1 >= frameNumber);
            if (!m_decoder->DecodeNextFrame(last, false, last)) break;
        }
        currentFrame = frameNumber;
    }

    dropAudioDuringStepping = false;
    RequestBwdPrefetch(currentFrame - 1);
}

void VideoPlayer::SetPlaybackSpeed(double speed)
{
    if (!std::isfinite(speed))
        return;
    speed = std::max(0.1, std::round(speed * 10.0) / 10.0);
    const double previous = m_playbackSpeed.exchange(speed, std::memory_order_acq_rel);
    if (isPlaying && std::fabs(previous - speed) > 0.0001)
    {
        m_playbackSpeedChangePending.store(true, std::memory_order_release);
        playbackWakeCondition.notify_all();
    }

    m_speedOverlayDeadline.store(GetTickCount64() + 4000, std::memory_order_release);
    if (videoWindow)
        InvalidateRect(videoWindow, nullptr, FALSE);
}

void VideoPlayer::ResetPlaybackClock(double pts)
{
    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_playbackClockStartPts.store(pts, std::memory_order_relaxed);
    m_playbackClockStartNs.store(nowNs, std::memory_order_release);
}

double VideoPlayer::GetPlaybackClockTarget() const
{
    const int64_t startNs = m_playbackClockStartNs.load(std::memory_order_acquire);
    const double startPts = m_playbackClockStartPts.load(std::memory_order_relaxed);
    if (startNs == 0)
        return startPts;

    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const double elapsed = std::max<int64_t>(0, nowNs - startNs) / 1000000000.0;
    return startPts + elapsed * GetPlaybackSpeed();
}

bool VideoPlayer::IsPlaybackSpeedOverlayVisible() const
{
    const ULONGLONG deadline = m_speedOverlayDeadline.load(std::memory_order_acquire);
    return deadline != 0 && GetTickCount64() < deadline;
}

void VideoPlayer::UpdatePlaybackSpeedOverlay()
{
    ULONGLONG deadline = m_speedOverlayDeadline.load(std::memory_order_acquire);
    if (deadline != 0 && GetTickCount64() >= deadline &&
        m_speedOverlayDeadline.compare_exchange_strong(deadline, 0, std::memory_order_acq_rel))
    {
        if (videoWindow)
            InvalidateRect(videoWindow, nullptr, FALSE);
    }
}

void VideoPlayer::StepFrame(int direction)
{
    if (!isLoaded || direction == 0)
        return;

    // Normal forward stepping starts from the displayed frame. If an
    // asynchronous forward cache miss is already pending, repeated presses
    // accumulate on that target instead of requesting the same frame again.
    if (direction > 0)
    {
        int64_t accumulatedTarget = -1;
        {
            std::lock_guard<std::mutex> lock(m_bwdPrefetch->mtx);
            if (m_bwdPrefetch->stepDirection > 0 &&
                m_bwdPrefetch->stepTargetFrame >= 0)
            {
                accumulatedTarget = m_bwdPrefetch->stepTargetFrame + 1;
                if (totalFrames > 0)
                    accumulatedTarget = std::min(accumulatedTarget, totalFrames - 1);
                m_bwdPrefetch->stepTargetFrame = accumulatedTarget;
            }
        }
        m_audioPlayer->StopThread();
        ClearPlaybackBuffer();
        if (accumulatedTarget >= 0)
        {
            RequestBwdPrefetch(accumulatedTarget);
            return;
        }

        CancelPendingSeekRefinement();
        SuspendBwdPrefetch();
        {
            std::lock_guard<std::mutex> lock(m_bwdPrefetch->mtx);
            m_bwdPrefetch->stepTargetFrame = -1;
            m_bwdPrefetch->stepDirection = 0;
        }
        int64_t target = currentFrame + 1;
        if (totalFrames > 0)
            target = std::min(target, totalFrames - 1);
        if (target == currentFrame)
            return;

        // The normal forward path remains the original one-packet decode.
        if (!m_decoderOutOfSync)
        {
            SeekToFrame(target);
            return;
        }

        extern bool g_improveSeekPerformance;
        if (g_improveSeekPerformance && ConsumeBwdPrefetch(target, false))
            return;

        // A frame displayed by the reverse cache leaves the main demuxer at a
        // different position. Never repair that state with an exact seek on
        // the UI thread: ask the existing CPU worker for the next frame.
        if (g_improveSeekPerformance)
        {
            {
                std::lock_guard<std::mutex> lock(m_bwdPrefetch->mtx);
                m_bwdPrefetch->stepTargetFrame = target;
                m_bwdPrefetch->stepDirection = 1;
            }
            RequestBwdPrefetch(target);
            return;
        }

        SeekToFrame(target);
        return;
    }

    bool startingReverseSequence = false;
    {
        std::lock_guard<std::mutex> lock(m_bwdPrefetch->mtx);
        startingReverseSequence = m_bwdPrefetch->stepTargetFrame < 0;
    }
    if (startingReverseSequence)
        CancelPendingSeekRefinement();

    int64_t target = 0;
    {
        std::lock_guard<std::mutex> lock(m_bwdPrefetch->mtx);
        const int64_t base = m_bwdPrefetch->stepTargetFrame >= 0
                                 ? m_bwdPrefetch->stepTargetFrame
                                 : currentFrame;
        target = std::max<int64_t>(0, base - 1);
        m_bwdPrefetch->stepTargetFrame = target;
        m_bwdPrefetch->stepDirection = -1;
    }

    if (target == currentFrame)
        return;

    extern bool g_improveSeekPerformance;
    if (g_improveSeekPerformance)
    {
        // Cache hits still display immediately. Cache misses never enter the
        // synchronous main-decoder seek path; the CPU worker will post the
        // requested frame back to this window when ready.
        if (ConsumeBwdPrefetch(target, false))
        {
            {
                std::lock_guard<std::mutex> lock(m_bwdPrefetch->mtx);
                m_bwdPrefetch->stepTargetFrame = -1;
                m_bwdPrefetch->stepDirection = 0;
            }
            RequestBwdPrefetch(currentFrame - 1);
            return;
        }

        RequestBwdPrefetch(target);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_bwdPrefetch->mtx);
        m_bwdPrefetch->stepTargetFrame = -1;
        m_bwdPrefetch->stepDirection = 0;
    }
    SeekToFrame(target);
}

void VideoPlayer::RequestBwdPrefetch(int64_t frame)
{
    extern bool g_improveSeekPerformance;
    if (!g_improveSeekPerformance || !isLoaded || frame < 0) return;
    {
        std::lock_guard<std::mutex> lk(m_bwdPrefetch->mtx);
        m_bwdPrefetch->suspended = false;
        m_bwdPrefetch->targetFrame = frame;
    }
    m_bwdPrefetch->cv.notify_one();
}

void VideoPlayer::SuspendBwdPrefetch()
{
    {
        std::lock_guard<std::mutex> lock(m_bwdPrefetch->mtx);
        m_bwdPrefetch->suspended = true;
        m_bwdPrefetch->targetFrame = -1;
    }
    m_bwdPrefetch->cv.notify_all();
}

bool VideoPlayer::ConsumeBwdPrefetch(int64_t frame, bool waitForFrame)
{
    extern bool g_improveSeekPerformance;
    if (!g_improveSeekPerformance) return false;

    std::unique_lock<std::mutex> lk(m_bwdPrefetch->mtx);
    if (waitForFrame && m_bwdPrefetch->cache.find(frame) == m_bwdPrefetch->cache.end())
    {
        // Give the already-running secondary decoder one frame interval to
        // finish its batch before falling back to a costly main-decoder seek.
        m_bwdPrefetch->targetFrame = frame;
        m_bwdPrefetch->cv.notify_one();
        m_bwdPrefetch->cv.wait_for(lk, std::chrono::milliseconds(24), [this, frame]() {
            return m_bwdPrefetch->exitFlag ||
                   m_bwdPrefetch->cache.find(frame) != m_bwdPrefetch->cache.end();
        });
    }

    auto it = m_bwdPrefetch->cache.find(frame);
    if (it != m_bwdPrefetch->cache.end()) {
        const auto& pixels = it->second.pixels;
        // Fast conversion from RGB24 (cache) back to BGRA (display buffer)
        // RGB24: [R, G, B, R, G, B, ...]
        // BGRA:  [B, G, R, A, B, G, R, A, ...]
        uint8_t* dst = buffer;
        const uint8_t* src = pixels.data();
        int count = frameWidth * frameHeight;
        if ((int)pixels.size() == count * 3) {
            {
                std::lock_guard<std::mutex> renderLock(renderMutex);
                for (int i = 0; i < count; ++i) {
                    dst[0] = src[2]; // B
                    dst[1] = src[1]; // G
                    dst[2] = src[0]; // R
                    dst[3] = 255;    // A
                    dst += 4;
                    src += 3;
                }
                displayUsesPlaybackBuffer = false;
            }
            currentFrame = frame;
            currentPts   = it->second.pts;
            m_decoderOutOfSync = true;
            m_bwdPrefetch->targetFrame = frame - 1; 
            UpdateCropForTime(currentPts);
            m_renderer->UpdateDisplay();
            UpdateTimeline();
            return true;
        }
    }
    return false;
}

void VideoPlayer::OnBwdFrameReady(int64_t frame)
{
    int direction = 0;
    int64_t requestedFrame = -1;
    {
        std::lock_guard<std::mutex> lock(m_bwdPrefetch->mtx);
        requestedFrame = m_bwdPrefetch->stepTargetFrame;
        direction = m_bwdPrefetch->stepDirection;
        const bool matchesRequest = direction > 0
                                        ? frame >= requestedFrame
                                        : frame == requestedFrame;
        if (requestedFrame < 0 || !matchesRequest)
            return;
    }
    if (!ConsumeBwdPrefetch(frame, false))
        return;

    {
        std::lock_guard<std::mutex> lock(m_bwdPrefetch->mtx);
        if (m_bwdPrefetch->stepTargetFrame == requestedFrame)
        {
            m_bwdPrefetch->stepTargetFrame = -1;
            m_bwdPrefetch->stepDirection = 0;
        }
    }
    if (direction > 0)
        SuspendBwdPrefetch();
    else
        RequestBwdPrefetch(currentFrame - 1);
    UpdateControls();
}

void VideoPlayer::BwdPrefetchThreadFunc()
{
    AVFormatContext* pfmt    = nullptr;
    AVCodecContext*  pcc     = nullptr;
    AVFrame*         phw     = nullptr;
    AVPacket*        ppkt    = nullptr;
    SwsContext*      psws    = nullptr;
    uint8_t*         pbuf    = nullptr;
    int              pbufSz  = 0;
    int              pvidIdx = -1;
    std::string      openFile;
    AVPixelFormat    pswsFmt = AV_PIX_FMT_NONE;

    auto closeCtx = [&]() {
        if (psws)  { sws_freeContext(psws); psws = nullptr; pswsFmt = AV_PIX_FMT_NONE; }
        if (pbuf)  { av_free(pbuf); pbuf = nullptr; pbufSz = 0; }
        if (ppkt)  { av_packet_free(&ppkt); ppkt = nullptr; }
        if (phw)   { av_frame_free(&phw);   phw  = nullptr; }
        if (pcc)   { avcodec_free_context(&pcc); }
        if (pfmt)  { avformat_close_input(&pfmt); }
        pvidIdx = -1; openFile.clear();
    };

    uint64_t lastGen = UINT64_MAX;
    int64_t lastTarget = -1;

    for (;;) {
        std::string lFile; double lOff, lFps; int lW, lH;
        int64_t lTarget; uint64_t lGen;
        {
            std::unique_lock<std::mutex> lk(m_bwdPrefetch->mtx);
            if (m_bwdPrefetch->suspended)
            {
                m_bwdPrefetch->cv.wait(lk, [&] {
                    return m_bwdPrefetch->exitFlag || !m_bwdPrefetch->suspended;
                });
            }
            else
            {
                m_bwdPrefetch->cv.wait_for(lk, std::chrono::milliseconds(20), [&] {
                    return m_bwdPrefetch->exitFlag ||
                           m_bwdPrefetch->workGen != lastGen ||
                           m_bwdPrefetch->targetFrame != lastTarget;
                });
            }
            if (m_bwdPrefetch->exitFlag) break;
            lFile   = m_bwdPrefetch->fileUtf8;
            lOff    = m_bwdPrefetch->startOff;
            lFps    = m_bwdPrefetch->fps;
            lW      = m_bwdPrefetch->sw;
            lH      = m_bwdPrefetch->sh;
            lTarget = m_bwdPrefetch->targetFrame;
            lGen    = m_bwdPrefetch->workGen;
        }

        if (lFile.empty()) { lastGen = lGen; lastTarget = lTarget; continue; }

        if (lFile != openFile || lGen != lastGen) {
            closeCtx();
            if (avformat_open_input(&pfmt, lFile.c_str(), nullptr, nullptr) < 0) { pfmt = nullptr; continue; }
            if (avformat_find_stream_info(pfmt, nullptr) < 0) { avformat_close_input(&pfmt); continue; }
            for (unsigned i = 0; i < (unsigned)pfmt->nb_streams; i++)
                if (pfmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { pvidIdx = (int)i; break; }
            if (pvidIdx < 0) { avformat_close_input(&pfmt); pfmt = nullptr; continue; }
            AVCodecParameters* cp = pfmt->streams[pvidIdx]->codecpar;
            const AVCodec* codec = avcodec_find_decoder(cp->codec_id);
            if (!codec) { avformat_close_input(&pfmt); pfmt = nullptr; continue; }
            pcc = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(pcc, cp);
            
            // Keep reverse predecode off the main hardware decoder. A bounded
            // CPU thread pool runs independently and cannot stall playback or
            // contend for the same D3D decoder surfaces.
            int numThreads = static_cast<int>(std::thread::hardware_concurrency() / 2);
            pcc->thread_count = std::clamp(numThreads, 2, 8);
            pcc->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

            if (avcodec_open2(pcc, codec, nullptr) < 0) { avcodec_free_context(&pcc); avformat_close_input(&pfmt); pfmt = nullptr; continue; }
            phw = av_frame_alloc(); ppkt = av_packet_alloc();

            // Keep the existing bounded RGB24 rolling window; no extra cache
            // is allocated for the asynchronous request path.
            pbufSz = av_image_get_buffer_size(AV_PIX_FMT_RGB24, lW, lH, 1);
            pbuf = (uint8_t*)av_malloc(pbufSz);
            openFile = lFile;
            lastGen = lGen;
        }

        if (lTarget < 0) { lastTarget = lTarget; continue; }

        int64_t missing = -1;
        {
            std::lock_guard<std::mutex> lk(m_bwdPrefetch->mtx);
            // Ultra-aggressive trimming for low memory: 
            // Keep exactly [lTarget - WINDOW, lTarget + 5]
            for (auto it = m_bwdPrefetch->cache.begin(); it != m_bwdPrefetch->cache.end(); ) {
                if (it->first > lTarget + 10 || it->first < lTarget - BwdPrefetch::WINDOW - 10) {
                    it = m_bwdPrefetch->cache.erase(it);
                } else ++it;
            }
            for (int64_t f = lTarget; f >= lTarget - BwdPrefetch::WINDOW && f >= 0; f--) {
                if (m_bwdPrefetch->cache.find(f) == m_bwdPrefetch->cache.end()) {
                    missing = f;
                    break;
                }
            }
        }

        if (missing < 0) { lastTarget = lTarget; continue; }

        double missingTime = lFps > 0 ? (missing / lFps) : 0.0;
        double seekTime    = std::max(0.0, missingTime - 0.5); // Tight seek
        AVStream* pvs = pfmt->streams[pvidIdx];
        int64_t   ts  = static_cast<int64_t>((seekTime + lOff) / av_q2d(pvs->time_base));
        
        av_seek_frame(pfmt, pvidIdx, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(pcc);

        while (true) {
            {
                std::lock_guard<std::mutex> lk(m_bwdPrefetch->mtx);
                if (m_bwdPrefetch->workGen != lGen) goto prefetch_reset_loop;
                if (m_bwdPrefetch->suspended) goto prefetch_reset_loop;
                // Do not abort merely because another frame-step arrived.
                // Completing this short batch is what keeps the rolling window
                // ahead of a continuously held backward key.
            }

            if (av_read_frame(pfmt, ppkt) < 0) break;
            if (ppkt->stream_index != pvidIdx) { av_packet_unref(ppkt); continue; }
            if (avcodec_send_packet(pcc, ppkt) < 0) { av_packet_unref(ppkt); continue; }
            av_packet_unref(ppkt);

            while (avcodec_receive_frame(pcc, phw) == 0) {
                double pts = 0.0;
                if (phw->best_effort_timestamp != AV_NOPTS_VALUE)
                    pts = phw->best_effort_timestamp * av_q2d(pvs->time_base) - lOff;
                else if (phw->pts != AV_NOPTS_VALUE)
                    pts = phw->pts * av_q2d(pvs->time_base) - lOff;
                
                int64_t f = static_cast<int64_t>(pts * lFps + 0.5);

                if (f >= lTarget - BwdPrefetch::WINDOW - 5 && f <= lTarget + 10) {
                    AVFrame* swFrame = phw;
                    AVPixelFormat fmt2 = (AVPixelFormat)swFrame->format;
                    if (fmt2 != pswsFmt) {
                        if (psws) sws_freeContext(psws);
                        // Convert to RGB24 in background for massive memory saving!
                        psws = sws_getContext(lW, lH, fmt2, lW, lH, AV_PIX_FMT_RGB24,
                                             SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                        pswsFmt = fmt2;
                    }
                    if (psws) {
                        uint8_t* d[4]{}; int ls[4]{};
                        av_image_fill_arrays(d, ls, pbuf, AV_PIX_FMT_RGB24, lW, lH, 1);
                        sws_scale(psws, (const uint8_t* const*)swFrame->data, swFrame->linesize, 0, lH, d, ls);
                        
                        BwdPrefetch::ReadyFrame rf;
                        rf.pts = pts;
                        rf.pixels.assign(pbuf, pbuf + pbufSz);

                        bool inserted = false;
                        bool requestedFrameReady = false;
                        {
                            std::lock_guard<std::mutex> lk(m_bwdPrefetch->mtx);
                            const int64_t liveTarget = m_bwdPrefetch->targetFrame;
                            if (m_bwdPrefetch->workGen == lGen &&
                                liveTarget >= 0 &&
                                f >= liveTarget - BwdPrefetch::WINDOW - 10 &&
                                f <= liveTarget + 10)
                            {
                                // Keep the same fixed-size rolling range even
                                // while the target is moving; never let old and
                                // new batches temporarily accumulate together.
                                for (auto it = m_bwdPrefetch->cache.begin();
                                     it != m_bwdPrefetch->cache.end();)
                                {
                                    if (it->first < liveTarget - BwdPrefetch::WINDOW - 10 ||
                                        it->first > liveTarget + 10)
                                        it = m_bwdPrefetch->cache.erase(it);
                                    else
                                        ++it;
                                }
                                m_bwdPrefetch->cache[f] = std::move(rf);
                                inserted = true;
                                requestedFrameReady =
                                    m_bwdPrefetch->stepDirection > 0
                                        ? (m_bwdPrefetch->stepTargetFrame >= 0 &&
                                           f >= m_bwdPrefetch->stepTargetFrame)
                                        : m_bwdPrefetch->stepTargetFrame == f;
                            }
                        }
                        if (inserted)
                            m_bwdPrefetch->cv.notify_all();
                        if (requestedFrameReady && videoWindow)
                            PostMessage(videoWindow, WM_BWD_FRAME_READY, 0,
                                        static_cast<LPARAM>(f));
                    }
                    if (swFrame != phw) {
                        av_frame_unref(swFrame);
                    }
                }
                av_frame_unref(phw);
                if (f > lTarget + 10) { goto gop_done_loop; }
            }
        }
    gop_done_loop:;
    prefetch_reset_loop:;
    }
    closeCtx();
}

bool VideoPlayer::SeekToTime(double seconds, int decodeCount, bool renderFastFrame, bool allowAsyncRefine)
{
    if (!isPlaying)
        m_resumeSeekPending.store(false, std::memory_order_release);
    bool reached = SeekToTimeInternal(seconds, decodeCount, allowAsyncRefine,
                                      false, false, renderFastFrame);
    if (!reached && decodeCount == INT_MAX && !allowAsyncRefine)
    {
        reached = SeekToTimeInternal(seconds, decodeCount, false, false, true,
                                     renderFastFrame, true, true);
    }
    return reached;
}

void VideoPlayer::SeekToTimeExact(double seconds)
{
    if (!isPlaying)
        m_resumeSeekPending.store(false, std::memory_order_release);
    if (!SeekToTimeInternal(seconds, INT_MAX, false, true))
        SeekToTimeInternal(seconds, INT_MAX, false, true, true,
                           true, true, true);
}

void VideoPlayer::SeekWhilePlaying(double seconds, bool exact)
{
    if (!isLoaded)
        return;
    if (!isPlaying)
    {
        SeekToTime(seconds);
        return;
    }

    if (seconds < 0.0)
        seconds = 0.0;
    if (duration > 0.0 && seconds >= duration)
    {
        const double frameDuration = frameRate > 0.0 ? 1.0 / frameRate : 0.033;
        seconds = std::max(0.0, duration - frameDuration);
    }

    // Store target first; release/acquire on the pending flag publishes it.
    // Repeated requests collapse to the newest target.
    {
        std::lock_guard<std::mutex> wakeLock(playbackWakeMutex);
        m_playbackSeekTarget.store(seconds, std::memory_order_relaxed);
        m_playbackSeekExact.store(exact, std::memory_order_relaxed);
        m_playbackSeekPending.store(true, std::memory_order_release);
        // Unlike the pending/in-progress flags, this generation cannot change
        // to a new value and back between two presentation-thread checks.
        m_playbackSeekGeneration.fetch_add(1, std::memory_order_release);
    }
    playbackWakeCondition.notify_all();
    playbackBufferCondition.notify_all();
}

bool VideoPlayer::SeekDemuxer(double seconds, AVStream* stream, int64_t streamTimestamp,
                              bool skipPrimarySeek)
{
    if (!formatContext || !stream || videoStreamIndex < 0)
        return false;

    const int64_t globalTimestamp = static_cast<int64_t>(std::llround(
        (seconds + startTimeOffset) * static_cast<double>(AV_TIME_BASE)));
    int seekResult = AVERROR(EIO);
    if (!skipPrimarySeek)
    {
#ifdef VIDEO_EDITOR_TESTING
        if (m_testNoOpNextPrimarySeek.exchange(false, std::memory_order_acq_rel))
        {
            m_testInjectedPrimarySeekNoOps.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        if (m_testFailNextPrimarySeek.exchange(false, std::memory_order_acq_rel))
        {
            m_testInjectedPrimarySeekFailures.fetch_add(1, std::memory_order_relaxed);
            seekResult = AVERROR(EIO);
        }
        else
#endif
        {
            seekResult = av_seek_frame(formatContext, videoStreamIndex,
                                       streamTimestamp, AVSEEK_FLAG_BACKWARD);
        }
        if (seekResult >= 0)
            return true;
    }

    // Some demuxers cannot service av_seek_frame with a stream timestamp even
    // though their more precise seek_file implementation works. This is common
    // with sparse/missing indexes and was previously ignored, leaving playback
    // to decode from an unrelated position while the timeline stayed pinned.
    seekResult = avformat_seek_file(formatContext, videoStreamIndex,
                                    INT64_MIN, streamTimestamp, streamTimestamp,
                                    AVSEEK_FLAG_BACKWARD);
    if (seekResult >= 0)
        return true;

    // Finally retry in AV_TIME_BASE units. Demuxers that do not support a
    // per-stream seek can still support a global container-timeline seek.
    seekResult = avformat_seek_file(formatContext, -1,
                                    INT64_MIN, globalTimestamp, globalTimestamp,
                                    AVSEEK_FLAG_BACKWARD);
    if (seekResult >= 0)
        return true;

    return av_seek_frame(formatContext, -1, globalTimestamp,
                         AVSEEK_FLAG_BACKWARD) >= 0;
}

bool VideoPlayer::SeekToTimeInternal(double seconds, int decodeCount, bool allowAsyncRefine,
                                     bool forceExact, bool hardSeek,
                                     bool renderFastFrame, bool abortOnPendingSeek,
                                     bool skipPrimarySeek,
                                     bool* seekPositionValid)
{
    if (seekPositionValid)
        *seekPositionValid = false;
    if (!isLoaded)
        return false;

    if (m_decoderOutOfSync) {
        hardSeek = true;
    }

    std::uint64_t generation = BeginSeekOperation();

    if (seconds < 0.0)
        seconds = 0.0;
    if (duration > 0.0 && seconds >= duration)
    {
        double frameDur = (frameRate > 0.0) ? (1.0 / frameRate) : 0.033;
        seconds = duration - frameDur;
        if (seconds < 0.0)
            seconds = 0.0;
    }

    const double frameDuration = (frameRate > 0.0) ? (1.0 / frameRate) : 0.033;
    bool smartSeek = false;

    {
        std::lock_guard<std::mutex> lock(decodeMutex);
        double threshold = frameDuration * 100.0;
        if (!hardSeek && seconds >= currentPts && seconds <= currentPts + threshold)
            smartSeek = true;
    }

    if (!smartSeek)
    {
        std::lock_guard<std::mutex> lock(decodeMutex);

        AVStream *vs = formatContext->streams[videoStreamIndex];
        int64_t ts = static_cast<int64_t>((seconds + startTimeOffset) / av_q2d(vs->time_base));
        const AVIndexEntry *entry = avformat_index_get_entry_from_timestamp(
            vs, ts, AVSEEK_FLAG_BACKWARD);

        if (!SeekDemuxer(seconds, vs, ts, skipPrimarySeek))
        {
            m_decoderOutOfSync = true;
            return false;
        }

        // Some demuxers report success without changing their read position.
        // When an index entry is available, reject that false success before
        // flushing the codec or decoding old-position packets. avio_tell can be
        // ahead by one IO buffer, so allow bounded buffering around entry->pos.
        if (!skipPrimarySeek && entry && entry->pos >= 0 && formatContext->pb)
        {
            const int64_t actualPosition = avio_tell(formatContext->pb);
            const int64_t ioTolerance = std::max<int64_t>(
                256 * 1024,
                formatContext->pb->buffer_size > 0
                    ? static_cast<int64_t>(formatContext->pb->buffer_size) * 4
                    : 0);
            if (actualPosition >= 0 &&
                std::llabs(actualPosition - entry->pos) > ioTolerance)
            {
                if (!SeekDemuxer(seconds, vs, ts, true))
                {
                    m_decoderOutOfSync = true;
                    return false;
                }
            }
        }

        m_decoder->ResetBufferedDecodeState();
        avcodec_flush_buffers(codecContext);
        m_decoderOutOfSync = false;

        for (auto &track : audioTracks)
        {
            if (track->codecContext)
                avcodec_flush_buffers(track->codecContext);
        }
        {
            std::lock_guard<std::mutex> audioLock(audioMutex);
            for (auto &tr : audioTracks)
                tr->buffer.clear();
        }

        if (entry)
        {
            int64_t keyTs = entry->timestamp;
            currentPts = keyTs * av_q2d(vs->time_base) - startTimeOffset;
            currentFrame = static_cast<int64_t>(currentPts * frameRate + 0.5);
        }
        else
        {
            currentPts = -1.0;
            currentFrame = 0;
        }
        UpdateCropForTime(currentPts);
    }

    const bool exactMode = forceExact || smartSeek;
    const int maxSyncFrames = exactMode ? INT_MAX : std::max(1, decodeCount);
    const int maxDecodeFrames = std::max(1000, std::min(maxSyncFrames, 10000));
    int decoded = 0;
    bool needAtLeastOneFrame = !smartSeek;

    // During seek catch-up, asking the demuxer to discard non-video streams
    // avoids walking and unref'ing every audio/subtitle packet in a long GOP.
    std::vector<AVDiscard> previousDiscard(formatContext->nb_streams);
    for (unsigned i = 0; i < formatContext->nb_streams; ++i)
    {
        previousDiscard[i] = formatContext->streams[i]->discard;
        if (static_cast<int>(i) != videoStreamIndex)
            formatContext->streams[i]->discard = AVDISCARD_ALL;
    }

    while (decoded < maxDecodeFrames)
    {
        // A newer asynchronous request supersedes this one. Return to the
        // playback loop immediately so rapid key presses never queue work.
        if (abortOnPendingSeek && isPlaying &&
            m_playbackSeekPending.load(std::memory_order_acquire))
            break;

        if (!needAtLeastOneFrame && currentPts >= seconds - (frameDuration * 0.5))
            break;
        if (!exactMode && decoded >= maxSyncFrames)
            break;

        double delta = seconds - currentPts;
        bool closeToTarget = delta < 0.2 || delta < (frameDuration * 5.0);
        bool isLastFastFrame = !exactMode && (decoded + 1 >= maxSyncFrames);
        
        if (!exactMode)
            codecContext->skip_frame = AVDISCARD_NONREF;
        else if (!closeToTarget && !isLastFastFrame)
            codecContext->skip_frame = AVDISCARD_NONREF;
        else
            codecContext->skip_frame = AVDISCARD_DEFAULT;

        dropAudioDuringStepping = true;
        bool decodeSuccess = m_decoder->DecodeNextFrame(false, false, closeToTarget || isLastFastFrame);
        dropAudioDuringStepping = false;
        
        if (!decodeSuccess)
            break;

        decoded++;
        needAtLeastOneFrame = false;
    }
    codecContext->skip_frame = AVDISCARD_DEFAULT;
    for (unsigned i = 0; i < formatContext->nb_streams; ++i)
        formatContext->streams[i]->discard = previousDiscard[i];

    // A demuxer can report seek success without moving. Do not trust the index
    // timestamp alone: a hard seek is successful only after decoding a frame
    // at (and not materially beyond) the requested position.
    const bool decodedAfterDemuxSeek = smartSeek || !needAtLeastOneFrame;
    const double validSeekLag = std::max(2.0, frameDuration * 60.0);
    const bool decodedInTargetRegion = decodedAfterDemuxSeek &&
                                       currentPts >= seconds - validSeekLag &&
                                       currentPts <= seconds + std::max(0.25, frameDuration * 3.0);
    if (seekPositionValid)
        *seekPositionValid = decodedInTargetRegion;
    const bool reachedTarget = decodedAfterDemuxSeek &&
                               currentPts >= seconds - (frameDuration * 0.5) &&
                               currentPts <= seconds + (frameDuration * 2.0);
    const bool superseded = abortOnPendingSeek && isPlaying &&
                            m_playbackSeekPending.load(std::memory_order_acquire);
    bool willRefineAsync = !reachedTarget && allowAsyncRefine && !forceExact && !isPlaying;

    // Present the inexpensive preview frame immediately. Background refinement
    // may continue decoding toward the exact timestamp, but it must not keep a
    // paused video visually frozen until key-up.
    if (!superseded && (reachedTarget || forceExact || renderFastFrame))
    {
        m_renderer->UpdateDisplay();
    }
    if (!superseded)
        UpdateTimeline();

    if (willRefineAsync)
        QueueSeekRefinement(seconds, generation);

    return reachedTarget;
}

double VideoPlayer::GetDuration() const
{
    return isLoaded ? duration : 0.0;
}

double VideoPlayer::GetCurrentTime() const
{
    return currentPts;
}

size_t VideoPlayer::GetBufferedPlaybackFrameCount() const
{
    std::lock_guard<std::mutex> lock(playbackBufferMutex);
    return playbackFrameBuffer.size();
}

bool VideoPlayer::HasPlaybackDecoderEnded() const
{
    std::lock_guard<std::mutex> lock(playbackBufferMutex);
    return playbackDecodeEof;
}

#ifdef VIDEO_EDITOR_TESTING
void VideoPlayer::ForceBufferedDecoderEagainAfterPacketsForTesting(int acceptedPackets)
{
    m_decoder->ForceBufferedSendEagainAfterPacketsForTesting(acceptedPackets);
}

uint64_t VideoPlayer::GetInjectedBufferedDecoderEagainCountForTesting() const
{
    return m_decoder->GetInjectedBufferedSendEagainCountForTesting();
}

void VideoPlayer::ForcePlaybackBufferCapacityForTesting(size_t capacity)
{
    m_testPlaybackBufferCapacity = std::max<size_t>(1, capacity);
}

void VideoPlayer::ForceNextPrimarySeekFailureForTesting()
{
    m_testFailNextPrimarySeek.store(true, std::memory_order_release);
}

uint64_t VideoPlayer::GetInjectedPrimarySeekFailureCountForTesting() const
{
    return m_testInjectedPrimarySeekFailures.load(std::memory_order_acquire);
}

void VideoPlayer::ForceNextPrimarySeekNoOpForTesting()
{
    m_testNoOpNextPrimarySeek.store(true, std::memory_order_release);
}

uint64_t VideoPlayer::GetInjectedPrimarySeekNoOpCountForTesting() const
{
    return m_testInjectedPrimarySeekNoOps.load(std::memory_order_acquire);
}
#endif

bool VideoPlayer::GetThumbnailPixels(double time, int dstW, int dstH, std::vector<uint8_t>& pixels) const
{
    if (!isLoaded || loadedFilename.empty() || dstW <= 0 || dstH <= 0)
        return false;

    int bufSize = WideCharToMultiByte(CP_UTF8, 0, loadedFilename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8File(bufSize, '\0');
    WideCharToMultiByte(CP_UTF8, 0, loadedFilename.c_str(), -1, &utf8File[0], bufSize, nullptr, nullptr);

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, utf8File.c_str(), nullptr, nullptr) < 0)
        return false;
    struct ScopedFmt { AVFormatContext* f; ~ScopedFmt() { avformat_close_input(&f); } } scopedFmt{fmt};

    if (avformat_find_stream_info(fmt, nullptr) < 0)
        return false;

    int vidIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++)
    {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            vidIdx = i;
            break;
        }
    }
    if (vidIdx < 0)
        return false;

    const AVCodec* codec = avcodec_find_decoder(fmt->streams[vidIdx]->codecpar->codec_id);
    if (!codec)
        return false;

    AVCodecContext* cc = avcodec_alloc_context3(codec);
    struct ScopedCC { AVCodecContext* c; ~ScopedCC() { avcodec_free_context(&c); } } scopedCC{cc};
    avcodec_parameters_to_context(cc, fmt->streams[vidIdx]->codecpar);
    
    AVPixelFormat hwFmtVal = AV_PIX_FMT_NONE;
    cc->opaque = &hwFmtVal;
    cc->get_format = [](AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
        AVPixelFormat* hwFmt = reinterpret_cast<AVPixelFormat*>(ctx->opaque);
        if (ctx->hw_device_ctx) {
            AVHWDeviceContext *hwctx = (AVHWDeviceContext*)ctx->hw_device_ctx->data;
            for (const enum AVPixelFormat *p = pix_fmts; *p != -1; p++) {
                if (hwctx->type == AV_HWDEVICE_TYPE_D3D11VA && *p == AV_PIX_FMT_D3D11) {
                    *hwFmt = *p;
                    return *p;
                }
                if (hwctx->type == AV_HWDEVICE_TYPE_DXVA2 && *p == AV_PIX_FMT_DXVA2_VLD) {
                    *hwFmt = *p;
                    return *p;
                }
            }
        }
        for (const enum AVPixelFormat *p = pix_fmts; *p != -1; p++) {
            if (*p != AV_PIX_FMT_D3D11 && *p != AV_PIX_FMT_DXVA2_VLD) {
                *hwFmt = AV_PIX_FMT_NONE;
                return *p;
            }
        }
        *hwFmt = pix_fmts[0];
        return pix_fmts[0];
    };

    bool ccUseHw = false;
    if (useHwAccel && hwDeviceCtx) {
        cc->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
        ccUseHw = true;
    }

    if (ccUseHw) {
        cc->thread_count = 1;
        cc->thread_type = FF_THREAD_SLICE;
    } else {
        int numThreads = std::thread::hardware_concurrency();
        if (numThreads < 1) numThreads = 1;
        cc->thread_count = numThreads;
        cc->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }

    if (avcodec_open2(cc, codec, nullptr) < 0)
        return false;

    // Compute start time offset (matches LoadVideo logic)
    double sto = 0.0;
    {
        double minStart = std::numeric_limits<double>::max();
        for (unsigned i = 0; i < fmt->nb_streams; ++i)
        {
            AVStream* s = fmt->streams[i];
            if (s->start_time != AV_NOPTS_VALUE)
            {
                double t = s->start_time * av_q2d(s->time_base);
                if (t < minStart) minStart = t;
            }
        }
        if (minStart != std::numeric_limits<double>::max())
            sto = minStart;
    }

    AVStream* vs = fmt->streams[vidIdx];
    double clampedTime = time;
    if (clampedTime < 0.0) clampedTime = 0.0;
    const double frameDur = (frameRate > 0.0) ? 1.0 / frameRate : 0.033;
    if (duration > 0.0 && clampedTime >= duration)
        clampedTime = duration - frameDur;
    if (clampedTime < 0.0) clampedTime = 0.0;

    int64_t ts = static_cast<int64_t>((clampedTime + sto) / av_q2d(vs->time_base));
    av_seek_frame(fmt, vidIdx, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(cc);

    AVFrame* frm = av_frame_alloc();
    struct ScopedFrm { AVFrame* f; ~ScopedFrm() { av_frame_free(&f); } } scopedFrm{frm};
    AVFrame* hwFrm = av_frame_alloc();
    struct ScopedHwFrm { AVFrame* f; ~ScopedHwFrm() { av_frame_free(&f); } } scopedHwFrm{hwFrm};
    AVPacket* pkt = av_packet_alloc();
    struct ScopedPkt { AVPacket* p; ~ScopedPkt() { av_packet_free(&p); } } scopedPkt{pkt};

    bool gotFrame = false;
    int maxPackets = 40;
    while (!gotFrame && maxPackets-- > 0)
    {
        if (av_read_frame(fmt, pkt) < 0) break;
        if (pkt->stream_index == vidIdx)
        {
            if (avcodec_send_packet(cc, pkt) >= 0)
            {
                if (avcodec_receive_frame(cc, ccUseHw ? hwFrm : frm) >= 0)
                    gotFrame = true;
            }
        }
        av_packet_unref(pkt);
    }

    if (!gotFrame)
    {
        avcodec_send_packet(cc, nullptr);
        gotFrame = (avcodec_receive_frame(cc, ccUseHw ? hwFrm : frm) >= 0);
    }

    if (gotFrame && ccUseHw)
    {
        if (hwFrm->format == hwFmtVal) {
            if (av_hwframe_transfer_data(frm, hwFrm, 0) < 0) {
                gotFrame = false;
            }
        } else {
            av_frame_unref(frm);
            av_frame_ref(frm, hwFrm);
        }
    }

    if (!gotFrame || frm->width <= 0 || frm->height <= 0)
        return false;

    SwsContext* sws = sws_getContext(
        frm->width, frm->height, (AVPixelFormat)frm->format,
        dstW, dstH, AV_PIX_FMT_BGRA,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws)
        return false;

    pixels.resize(static_cast<size_t>(dstW) * dstH * 4);
    uint8_t* dst[1]    = { pixels.data() };
    int      dstStride[1] = { dstW * 4 };
    sws_scale(sws, frm->data, frm->linesize, 0, frm->height, dst, dstStride);
    sws_freeContext(sws);
    return true;
}

void VideoPlayer::InitThumbnailCtx()
{
    CleanupThumbnailCtx();

    if (loadedFilename.empty()) return;

    int bufSize = WideCharToMultiByte(CP_UTF8, 0, loadedFilename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8File(bufSize, '\0');
    WideCharToMultiByte(CP_UTF8, 0, loadedFilename.c_str(), -1, &utf8File[0], bufSize, nullptr, nullptr);

    auto ctx = std::make_unique<ThumbCtx>();

    if (avformat_open_input(&ctx->fmt, utf8File.c_str(), nullptr, nullptr) < 0) return;
    if (avformat_find_stream_info(ctx->fmt, nullptr) < 0) {
        avformat_close_input(&ctx->fmt); return;
    }

    for (unsigned i = 0; i < ctx->fmt->nb_streams; i++) {
        if (ctx->fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            ctx->vidIdx = i; break;
        }
    }
    if (ctx->vidIdx < 0) { avformat_close_input(&ctx->fmt); return; }

    const AVCodec* codec = avcodec_find_decoder(ctx->fmt->streams[ctx->vidIdx]->codecpar->codec_id);
    if (!codec) { avformat_close_input(&ctx->fmt); return; }

    ctx->cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx->cc, ctx->fmt->streams[ctx->vidIdx]->codecpar);
    
    ctx->useHwAccel = false;
    ctx->hwPixelFormat = AV_PIX_FMT_NONE;
    ctx->cc->opaque = ctx.get();
    ctx->cc->get_format = [](AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
        ThumbCtx* thumb = reinterpret_cast<ThumbCtx*>(ctx->opaque);
        if (ctx->hw_device_ctx) {
            AVHWDeviceContext *hwctx = (AVHWDeviceContext*)ctx->hw_device_ctx->data;
            for (const enum AVPixelFormat *p = pix_fmts; *p != -1; p++) {
                if (hwctx->type == AV_HWDEVICE_TYPE_D3D11VA && *p == AV_PIX_FMT_D3D11) {
                    thumb->hwPixelFormat = *p;
                    return *p;
                }
                if (hwctx->type == AV_HWDEVICE_TYPE_DXVA2 && *p == AV_PIX_FMT_DXVA2_VLD) {
                    thumb->hwPixelFormat = *p;
                    return *p;
                }
            }
        }
        for (const enum AVPixelFormat *p = pix_fmts; *p != -1; p++) {
            if (*p != AV_PIX_FMT_D3D11 && *p != AV_PIX_FMT_DXVA2_VLD) {
                thumb->hwPixelFormat = AV_PIX_FMT_NONE;
                return *p;
            }
        }
        thumb->hwPixelFormat = pix_fmts[0];
        return pix_fmts[0];
    };

    if (useHwAccel && hwDeviceCtx) {
        ctx->cc->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
        ctx->useHwAccel = true;
    }

    if (ctx->useHwAccel) {
        ctx->cc->thread_count = 1;
        ctx->cc->thread_type = FF_THREAD_SLICE;
    } else {
        int numThreads = std::thread::hardware_concurrency();
        if (numThreads < 1) numThreads = 1;
        ctx->cc->thread_count = numThreads;
        ctx->cc->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }

    if (avcodec_open2(ctx->cc, codec, nullptr) < 0) {
        avcodec_free_context(&ctx->cc); avformat_close_input(&ctx->fmt); return;
    }

    ctx->frm = av_frame_alloc();
    ctx->hwFrm = av_frame_alloc();
    ctx->pkt = av_packet_alloc();
    if (!ctx->frm || !ctx->hwFrm || !ctx->pkt) {
        av_frame_free(&ctx->frm);
        av_frame_free(&ctx->hwFrm);
        av_packet_free(&ctx->pkt);
        avcodec_free_context(&ctx->cc);
        avformat_close_input(&ctx->fmt);
        return;
    }

    // Compute startTimeOffset (same logic as LoadVideo)
    double minStart = std::numeric_limits<double>::max();
    for (unsigned i = 0; i < ctx->fmt->nb_streams; ++i) {
        AVStream* s = ctx->fmt->streams[i];
        if (s->start_time != AV_NOPTS_VALUE) {
            double t = s->start_time * av_q2d(s->time_base);
            if (t < minStart) minStart = t;
        }
    }
    if (minStart != std::numeric_limits<double>::max())
        ctx->sto = minStart;

    m_thumbCtx = std::move(ctx);
}

void VideoPlayer::CleanupThumbnailCtx()
{
    if (!m_thumbCtx) return;
    std::lock_guard<std::mutex> lck(m_thumbCtx->mtx);
    if (m_thumbCtx->sws) { sws_freeContext(m_thumbCtx->sws); m_thumbCtx->sws = nullptr; }
    if (m_thumbCtx->frm) { av_frame_free(&m_thumbCtx->frm); }
    if (m_thumbCtx->hwFrm) { av_frame_free(&m_thumbCtx->hwFrm); }
    if (m_thumbCtx->pkt) { av_packet_free(&m_thumbCtx->pkt); }
    if (m_thumbCtx->cc)  { avcodec_free_context(&m_thumbCtx->cc); }
    if (m_thumbCtx->fmt) { avformat_close_input(&m_thumbCtx->fmt); }
    m_thumbCtx.reset();
}

bool VideoPlayer::GetThumbnailPixelsFast(double time, int dstW, int dstH, std::vector<uint8_t>& pixels)
{
    if (!m_thumbCtx || dstW <= 0 || dstH <= 0) return false;

    std::lock_guard<std::mutex> lck(m_thumbCtx->mtx);
    if (!m_thumbCtx->fmt || !m_thumbCtx->cc || !m_thumbCtx->frm || !m_thumbCtx->pkt)
        return false;

    ThumbCtx* ctx = m_thumbCtx.get();
    AVStream* vs  = ctx->fmt->streams[ctx->vidIdx];

    double clampedTime = time;
    if (clampedTime < 0.0) clampedTime = 0.0;
    const double frameDur = (frameRate > 0.0) ? 1.0 / frameRate : 0.033;
    if (duration > 0.0 && clampedTime >= duration)
        clampedTime = duration - frameDur;
    if (clampedTime < 0.0) clampedTime = 0.0;

    int64_t ts = static_cast<int64_t>((clampedTime + ctx->sto) / av_q2d(vs->time_base));
    av_seek_frame(ctx->fmt, ctx->vidIdx, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(ctx->cc);

    bool gotFrame = false;
    int maxPackets = 40;
    while (!gotFrame && maxPackets-- > 0) {
        av_frame_unref(ctx->frm);
        av_frame_unref(ctx->hwFrm);
        if (av_read_frame(ctx->fmt, ctx->pkt) < 0) break;
        if (ctx->pkt->stream_index == ctx->vidIdx) {
            if (avcodec_send_packet(ctx->cc, ctx->pkt) >= 0) {
                if (avcodec_receive_frame(ctx->cc, ctx->useHwAccel ? ctx->hwFrm : ctx->frm) >= 0)
                    gotFrame = true;
            }
        }
        av_packet_unref(ctx->pkt);
    }
    if (!gotFrame) {
        av_frame_unref(ctx->frm);
        av_frame_unref(ctx->hwFrm);
        avcodec_send_packet(ctx->cc, nullptr);
        gotFrame = (avcodec_receive_frame(ctx->cc, ctx->useHwAccel ? ctx->hwFrm : ctx->frm) >= 0);
        avcodec_flush_buffers(ctx->cc);
    }

    if (gotFrame && ctx->useHwAccel) {
        if (ctx->hwFrm->format == ctx->hwPixelFormat) {
            if (av_hwframe_transfer_data(ctx->frm, ctx->hwFrm, 0) < 0) {
                gotFrame = false;
            }
        } else {
            av_frame_unref(ctx->frm);
            av_frame_ref(ctx->frm, ctx->hwFrm);
        }
    }

    if (!gotFrame) return false;

    AVFrame* swFrame = ctx->frm;
    if (swFrame->width <= 0 || swFrame->height <= 0) return false;

    // Recreate SwsContext only when dimensions change
    if (!ctx->sws || ctx->lastDstW != dstW || ctx->lastDstH != dstH) {
        if (ctx->sws) sws_freeContext(ctx->sws);
        ctx->sws = sws_getContext(ctx->frm->width, ctx->frm->height, (AVPixelFormat)ctx->frm->format,
                                   dstW, dstH, AV_PIX_FMT_BGRA,
                                   SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        ctx->lastDstW = dstW;
        ctx->lastDstH = dstH;
    }
    if (!ctx->sws) return false;

    pixels.resize(static_cast<size_t>(dstW) * dstH * 4);
    uint8_t* dstData[1]   = { pixels.data() };
    int      dstStride[1] = { dstW * 4 };
    sws_scale(ctx->sws, ctx->frm->data, ctx->frm->linesize, 0, ctx->frm->height, dstData, dstStride);
    
    av_frame_unref(ctx->frm);
    av_frame_unref(ctx->hwFrm);
    return true;
}

void VideoPlayer::SetPosition(int x, int y, int width, int height)
{
    m_renderer->SetPosition(x, y, width, height);
}

void VideoPlayer::Render()
{
    m_renderer->Render();
}

void VideoPlayer::ForceRedraw()
{
    if (m_renderer)
        m_renderer->UpdateDisplay();
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
            if (!AdvanceClipPreview()) {
                clipPreviewActive = false;
                Pause();
                UpdateControls();
            }
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
    }
}

void VideoPlayer::ClearPlaybackBuffer()
{
    {
        std::lock_guard<std::mutex> lock(playbackBufferMutex);
        playbackFrameBuffer.clear();
        playbackDecodeEof = false;
    }
    playbackBufferCondition.notify_all();
}

void VideoPlayer::PlaybackDecodeThreadFunction()
{
    while (playbackThreadRunning)
    {
        if (m_playbackSeekPending.load(std::memory_order_acquire))
        {
            // Publish the transition before clearing the pending flag so the
            // presentation thread can never consume an old queued frame in
            // the small hand-off window.
            m_playbackSeekInProgress.store(true, std::memory_order_release);
            if (!m_playbackSeekPending.exchange(false, std::memory_order_acq_rel))
            {
                m_playbackSeekInProgress.store(false, std::memory_order_release);
                continue;
            }
            m_audioPlayer->StopThread(true);
            ClearPlaybackBuffer();
            // Decode cadence is timestamp-based at high speed. A backward
            // seek must forget the last pre-seek delivery timestamp or every
            // frame before that old position is rejected and the producer can
            // run all the way back to EOF without refilling the queue.
            m_lastHighSpeedFrameDeliveryNs = 0;

            do
            {
                double target = 0.0;
                bool exact = true;
                {
                    std::lock_guard<std::mutex> wakeLock(playbackWakeMutex);
                    target = m_playbackSeekTarget.load(std::memory_order_acquire);
                    exact = m_playbackSeekExact.load(std::memory_order_acquire);
                }
                const double positionBeforeSeek = currentPts;
                bool seekPositionValid = false;
                SeekToTimeInternal(
                    target, exact ? INT_MAX : 12, false, exact,
                    true, true, exact, false, &seekPositionValid);

                // av_seek_frame can return success without moving the demuxer.
                // The decoded-position validation above catches that lie; retry
                // via avformat_seek_file instead of playing from the old/start
                // position while the UI remains pinned to the requested time.
                if (!seekPositionValid &&
                    !m_playbackSeekPending.load(std::memory_order_acquire))
                {
                    SeekToTimeInternal(
                        target, exact ? INT_MAX : 12, false, exact,
                        true, true, exact, true, &seekPositionValid);
                }

                // If neither seek API can reach the target, retain the last
                // playable position rather than leaving the decoder at EOF or
                // an unrelated beginning position.
                if (!seekPositionValid &&
                    !m_playbackSeekPending.load(std::memory_order_acquire))
                {
                    SeekToTimeInternal(positionBeforeSeek, INT_MAX, false, true,
                                       true, false, true, true);
                }
            }
            while (m_playbackSeekPending.exchange(false, std::memory_order_acq_rel));

            // A presentation iteration that began just before seekInProgress
            // was published may have started audio after the first StopThread.
            // Close that race before allowing the new post-seek clock to run.
            m_audioPlayer->StopThread(true);
            masterStartPts = currentPts;
            masterStartTime = std::chrono::high_resolution_clock::now();
            m_playbackClockStartPts.store(currentPts, std::memory_order_relaxed);
            m_playbackClockStartNs.store(0, std::memory_order_release);
            m_playbackSeekInProgress.store(false, std::memory_order_release);
            playbackBufferCondition.notify_all();
            continue;
        }

        {
            std::unique_lock<std::mutex> lock(playbackBufferMutex);
            playbackBufferCondition.wait(lock, [this]() {
                return !playbackThreadRunning ||
                       m_playbackSeekPending.load(std::memory_order_acquire) ||
                       playbackFrameBuffer.size() < playbackBufferCapacity;
            });
            if (!playbackThreadRunning)
                return;
            if (m_playbackSeekPending.load(std::memory_order_acquire))
                continue;
        }

        BufferedPlaybackFrame bufferedFrame;
        bufferedFrame.frame = av_frame_alloc();
        if (!bufferedFrame.frame)
            return;

        const VideoDecoder::BufferedDecodeResult decodeResult =
            m_decoder->DecodeNextBufferedFrame(bufferedFrame.frame,
                                               bufferedFrame.pts,
                                               bufferedFrame.frameNumber);
        if (decodeResult == VideoDecoder::BufferedDecodeResult::RecoverableError)
        {
            // A bad packet or a transient hardware-frame transfer must not
            // permanently kill playback. The demuxer/decoder consumed the bad
            // input, so retrying advances to the next usable frame.
            std::this_thread::yield();
            continue;
        }
        if (decodeResult == VideoDecoder::BufferedDecodeResult::EndOfStream)
        {
            std::unique_lock<std::mutex> lock(playbackBufferMutex);
            playbackDecodeEof = true;
            playbackBufferCondition.notify_all();

            // Keep the producer alive until presentation has stopped playback
            // or an in-playback seek supplies a new decode position. Returning
            // here left seekPending with no thread to service it, so the player
            // could drain its three queued frames and become unrecoverable.
            playbackBufferCondition.wait(lock, [this]() {
                return !playbackThreadRunning ||
                       m_playbackSeekPending.load(std::memory_order_acquire);
            });
            if (!playbackThreadRunning)
                return;
            continue;
        }

        if (m_playbackSeekPending.load(std::memory_order_acquire))
            continue;

        {
            std::lock_guard<std::mutex> lock(playbackBufferMutex);
            playbackFrameBuffer.emplace_back(std::move(bufferedFrame));
        }
        playbackBufferCondition.notify_all();
    }
}

bool VideoPlayer::PresentBufferedFrame(BufferedPlaybackFrame&& bufferedFrame)
{
    if (!bufferedFrame.frame)
        return false;

    currentPts = bufferedFrame.pts;
    currentFrame = bufferedFrame.frameNumber;
    const bool cropChanged = UpdateCropForTime(currentPts);

    {
        std::lock_guard<std::mutex> renderLock(renderMutex);
        const AVPixelFormat actualFormat = static_cast<AVPixelFormat>(bufferedFrame.frame->format);
        const bool usePreviewBuffer = GetPlaybackSpeed() >= 4.0;
        if (usePreviewBuffer)
        {
            RECT clientRect{};
            GetClientRect(videoWindow, &clientRect);
            const int clientWidth = std::max(1L, clientRect.right - clientRect.left);
            const int clientHeight = std::max(1L, clientRect.bottom - clientRect.top);
            const double scale = std::min({
                1.0,
                clientWidth / static_cast<double>(std::max(1, frameWidth)),
                clientHeight / static_cast<double>(std::max(1, frameHeight))});
            const int previewWidth = std::max(2, static_cast<int>(std::lround(frameWidth * scale)));
            const int previewHeight = std::max(2, static_cast<int>(std::lround(frameHeight * scale)));

            if (actualFormat != playbackSwsSourceFormat ||
                previewWidth != playbackRgbWidth || previewHeight != playbackRgbHeight)
            {
                sws_freeContext(playbackSwsContext);
                playbackSwsSourceFormat = actualFormat;
                playbackRgbWidth = previewWidth;
                playbackRgbHeight = previewHeight;
                playbackRgbStride = previewWidth * 4;
                playbackRgbBuffer.resize(
                    static_cast<size_t>(playbackRgbStride) * playbackRgbHeight);
                playbackSwsContext = sws_getContext(
                    frameWidth, frameHeight, actualFormat,
                    playbackRgbWidth, playbackRgbHeight, AV_PIX_FMT_BGRA,
                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
            }
            if (!playbackSwsContext || playbackRgbBuffer.empty())
                return false;

            uint8_t* destinationData[4] = { playbackRgbBuffer.data(), nullptr, nullptr, nullptr };
            int destinationStride[4] = { playbackRgbStride, 0, 0, 0 };
            sws_scale(playbackSwsContext,
                      const_cast<const uint8_t* const*>(bufferedFrame.frame->data),
                      bufferedFrame.frame->linesize, 0, frameHeight,
                      destinationData, destinationStride);
            displayUsesPlaybackBuffer = true;
        }
        else
        {
            if (actualFormat != swsSourceFormat && actualFormat != AV_PIX_FMT_NONE)
            {
                sws_freeContext(swsContext);
                swsSourceFormat = actualFormat;
                swsContext = sws_getContext(
                    frameWidth, frameHeight, actualFormat,
                    frameWidth, frameHeight, AV_PIX_FMT_BGRA,
                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
            }
            if (!swsContext)
                return false;

            sws_scale(swsContext,
                      const_cast<const uint8_t* const*>(bufferedFrame.frame->data),
                      bufferedFrame.frame->linesize, 0, frameHeight,
                      frameRGB->data, frameRGB->linesize);
            displayUsesPlaybackBuffer = false;
        }
    }

    m_presentedPlaybackFrameCount.fetch_add(1, std::memory_order_release);
    InvalidateRect(videoWindow, nullptr, FALSE);
    UpdateTimeline();
    if (cropChanged)
        InvalidateRect(videoWindow, nullptr, FALSE);
    return true;
}

void VideoPlayer::PlaybackThreadFunction()
{
    bool clockStarted = false;
    auto startTime = masterStartTime;
    double startPts = masterStartPts;
    uint64_t observedSeekGeneration =
        m_playbackSeekGeneration.load(std::memory_order_acquire);

    while (playbackThreadRunning)
    {
        const uint64_t loopSeekGeneration =
            m_playbackSeekGeneration.load(std::memory_order_acquire);
        if (loopSeekGeneration != observedSeekGeneration)
        {
            observedSeekGeneration = loopSeekGeneration;
            clockStarted = false;
        }

        BufferedPlaybackFrame bufferedFrame;
        {
            std::unique_lock<std::mutex> lock(playbackBufferMutex);
            playbackBufferCondition.wait(lock, [this, clockStarted]() {
                const size_t required = clockStarted ? 1 : playbackPrebufferFrames;
                return !playbackThreadRunning ||
                       m_playbackSeekPending.load(std::memory_order_acquire) ||
                       m_playbackSeekInProgress.load(std::memory_order_acquire) ||
                       playbackDecodeEof || playbackFrameBuffer.size() >= required;
            });

            if (!playbackThreadRunning)
                return;
            if (m_playbackSeekPending.load(std::memory_order_acquire) ||
                m_playbackSeekInProgress.load(std::memory_order_acquire))
            {
                clockStarted = false;
                playbackBufferCondition.notify_all();
                playbackBufferCondition.wait(lock, [this]() {
                    return !playbackThreadRunning ||
                           (!m_playbackSeekPending.load(std::memory_order_acquire) &&
                            !m_playbackSeekInProgress.load(std::memory_order_acquire));
                });
                continue;
            }
            if (playbackFrameBuffer.empty())
            {
                if (playbackDecodeEof)
                {
                    // EOF and a user seek can arrive together. Give the seek
                    // publisher a short hand-off window and re-check while
                    // holding the queue lock; otherwise this thread can decide
                    // to Stop just as the producer clears EOF and restarts at
                    // the new position, killing the recovered playback.
                    playbackBufferCondition.wait_for(
                        lock, std::chrono::milliseconds(50), [this]() {
                            return !playbackThreadRunning ||
                                   m_playbackSeekPending.load(std::memory_order_acquire) ||
                                   m_playbackSeekInProgress.load(std::memory_order_acquire) ||
                                   !playbackDecodeEof || !playbackFrameBuffer.empty();
                        });
                    if (!playbackThreadRunning)
                        return;
                    if (m_playbackSeekPending.load(std::memory_order_acquire) ||
                        m_playbackSeekInProgress.load(std::memory_order_acquire) ||
                        !playbackDecodeEof || !playbackFrameBuffer.empty())
                    {
                        clockStarted = false;
                        continue;
                    }
                    lock.unlock();
                    Stop();
                    PostMessage(parentWindow, WM_TIMER, 1006, 0);
                    return;
                }
                continue;
            }

            bufferedFrame = std::move(playbackFrameBuffer.front());
            playbackFrameBuffer.pop_front();
        }
        playbackBufferCondition.notify_all();

        // The producer can process an entire seek while this thread is between
        // checks, making both transient flags false again. The generation still
        // changes, so discard the frame from the old clock epoch and restart.
        uint64_t latestSeekGeneration =
            m_playbackSeekGeneration.load(std::memory_order_acquire);
        if (latestSeekGeneration != observedSeekGeneration)
        {
            observedSeekGeneration = latestSeekGeneration;
            clockStarted = false;
            continue;
        }

        if (!clockStarted)
        {
            // The seek flag can change after a frame is removed from the
            // buffer. Do not start audio for that stale presentation cycle.
            if (m_playbackSeekPending.load(std::memory_order_acquire) ||
                m_playbackSeekInProgress.load(std::memory_order_acquire))
                continue;

            startTime = std::chrono::high_resolution_clock::now();
            startPts = currentPts;
            masterStartTime = startTime;
            masterStartPts = startPts;
            ResetPlaybackClock(startPts);
            if (GetPlaybackSpeed() < 4.0)
                m_audioPlayer->StartThread();
            clockStarted = true;
        }

        // A speed change alters the relationship between media timestamps and
        // wall-clock time. Re-anchor both clocks at the last presented frame so
        // the new rate applies only from this point forward (and so audio does
        // not reinterpret samples already submitted at the previous rate).
        if (m_playbackSpeedChangePending.exchange(false, std::memory_order_acq_rel))
        {
            m_audioPlayer->StopThread(true);
            startTime = std::chrono::high_resolution_clock::now();
            startPts = currentPts;
            masterStartTime = startTime;
            masterStartPts = startPts;
            ResetPlaybackClock(startPts);
            if (playbackThreadRunning && isPlaying && GetPlaybackSpeed() < 4.0)
                m_audioPlayer->StartThread();
        }

        const double speed = GetPlaybackSpeed();
        const auto now = std::chrono::high_resolution_clock::now();
        const double elapsed = std::chrono::duration<double>(now - startTime).count();

        const double target = (bufferedFrame.pts - startPts) / speed;
        const double delay = target - elapsed;
        if (delay > 0.0)
        {
            std::unique_lock<std::mutex> wakeLock(playbackWakeMutex);
            playbackWakeCondition.wait_for(
                wakeLock, std::chrono::duration<double>(delay), [this]() {
                    return !playbackThreadRunning ||
                           m_playbackSeekPending.load(std::memory_order_acquire) ||
                           m_playbackSpeedChangePending.load(std::memory_order_acquire);
                });
        }

        if (!playbackThreadRunning)
            return;
        if (m_playbackSeekPending.load(std::memory_order_acquire))
        {
            clockStarted = false;
            continue;
        }

        latestSeekGeneration =
            m_playbackSeekGeneration.load(std::memory_order_acquire);
        if (latestSeekGeneration != observedSeekGeneration)
        {
            observedSeekGeneration = latestSeekGeneration;
            clockStarted = false;
            continue;
        }

        if (!PresentBufferedFrame(std::move(bufferedFrame)))
            continue;

        if (clipPreviewActive && currentPts >= clipPreviewEndTime)
        {
            if (AdvanceClipPreview())
            {
                clockStarted = false;
                continue;
            }
            clipPreviewActive = false;
            Pause();
            PostMessage(parentWindow, WM_TIMER, 1006, 0);
            return;
        }
    }
}

bool VideoPlayer::CutVideo(const std::wstring &outputFilename, double startTime,
                           double endTime, bool mergeAudio, bool convertH264,
                           EncoderSelection encoder, const std::wstring& qualityPreset, int maxBitrate, HWND progressBar,
                           std::atomic<bool>* cancelFlag)
{
    return m_cutter->CutVideo(outputFilename, startTime, endTime, mergeAudio, convertH264, encoder, qualityPreset, maxBitrate, progressBar, cancelFlag);
}

bool VideoPlayer::CutVideo(const std::wstring &outputFilename, const std::vector<ClipSegment>& segments,
                           bool mergeAudio, bool convertH264, EncoderSelection encoder,
                           const std::wstring& qualityPreset, int maxBitrate, HWND progressBar,
                           std::atomic<bool>* cancelFlag)
{
    return m_cutter->CutVideo(outputFilename, segments, mergeAudio, convertH264, encoder,
                              qualityPreset, maxBitrate, progressBar, cancelFlag);
}

LRESULT CALLBACK VideoPlayer::VideoWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    VideoPlayer* player = reinterpret_cast<VideoPlayer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (player)
    {
        if (msg == WM_BWD_FRAME_READY)
        {
            player->OnBwdFrameReady(static_cast<int64_t>(lParam));
            return 0;
        }
        else if (msg == WM_PAINT)
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
