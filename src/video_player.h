#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <d2d1.h>
#include <dwrite.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/rational.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
}

#include <rnnoise.h>

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <chrono>
#include <limits>
#include <cstdint>
#include "clip_segment.h"

enum class EncoderSelection : int;

// Audio output using Windows Audio Session API (WASAPI)
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>

class VideoDecoder;
class AudioPlayer;
class VideoRenderer;
class VideoCutter;

// Audio track structure
struct AudioTrack {
    int streamIndex;
    AVCodecContext *codecContext;
    SwrContext *swrContext;
    AVFrame *frame;
    bool isMuted;
    float volume;
    std::string name;
    std::deque<int16_t> buffer;
    std::vector<int16_t> resampleBuffer;
    double bufferPts;
    
    // Voice isolation support
    bool voiceIsolationEnabled;
    DenoiseState* denoiseState;
    SwrContext* voiceIsolationSwrContext;  // For 48kHz mono conversion
    SwrContext* voiceIsolationBackSwrContext; // For converting back to original format
    std::vector<float> voiceIsolationInputBuffer;   // Input buffer for RNNoise (480 samples)
    std::vector<int16_t> voiceIsolationMonoBuffer;  // 48kHz mono buffer
    std::vector<int16_t> voiceIsolationProcessedBuffer; // Processed 48kHz mono buffer
    std::deque<float> voiceIsolationSampleQueue;    // Queue for incomplete frames

    AudioTrack() : streamIndex(-1), codecContext(nullptr), swrContext(nullptr),
                   frame(nullptr), isMuted(false), volume(1.0f), bufferPts(0.0),
                   voiceIsolationEnabled(false), denoiseState(nullptr), 
                   voiceIsolationSwrContext(nullptr), voiceIsolationBackSwrContext(nullptr) {}
};

class VideoPlayer
{
    friend class VideoDecoder;
    friend class AudioPlayer;
    friend class VideoRenderer;
    friend class VideoCutter;

public:
    AVFormatContext *formatContext;
    AVCodecContext *codecContext;
    AVFrame *frame;
    AVFrame *frameRGB;
    AVFrame *hwFrame; // Frame for hardware decoding
    AVBufferRef *hwDeviceCtx;
    AVPixelFormat hwPixelFormat;
    bool useHwAccel;
    AVPacket *packet;
    struct SwsContext *swsContext;
    AVPixelFormat swsSourceFormat;
    uint8_t *buffer;
    int rgbBufferSize;
    struct SwsContext *playbackSwsContext;
    AVPixelFormat playbackSwsSourceFormat;
    std::vector<uint8_t> playbackRgbBuffer;
    int playbackRgbWidth;
    int playbackRgbHeight;
    int playbackRgbStride;
    bool displayUsesPlaybackBuffer;
    int videoStreamIndex;
    int frameWidth, frameHeight;
    bool isLoaded;
    bool isPlaying;
    double frameRate;
    int64_t currentFrame;
    int64_t totalFrames;
    double currentPts;
    double duration;
    double startTimeOffset;
    bool clipPreviewActive;
    double clipPreviewEndTime;
    std::vector<ClipSegment> clipPreviewSegments;
    size_t clipPreviewSegmentIndex = 0;

    // Crop selection
    struct CropKeyframe {
        double time;
        RECT rect;
        bool enabled;
    };
    RECT cropRect;
    std::vector<CropKeyframe> cropTimeline;
    bool hasCrop;
    int cropOutputWidth;
    int cropOutputHeight;
    mutable std::mutex cropMutex;
    bool selectingCrop;
    POINT cropStart;
    POINT cropCurrent;

    HWND parentWindow;
    HWND videoWindow;
    WNDPROC originalVideoWndProc;

    // Direct2D rendering components
    ID2D1Factory* d2dFactory;
    ID2D1HwndRenderTarget* d2dRenderTarget;
    ID2D1Bitmap* d2dBitmap;
    IDWriteFactory* dwriteFactory;
    IDWriteTextFormat* speedTextFormat;

    // Timer for playback
    UINT_PTR playbackTimer;
    // Threaded playback
    std::thread playbackThread;
    std::thread playbackDecodeThread;
    std::atomic<bool> playbackThreadRunning;
    std::mutex playbackWakeMutex;
    std::condition_variable playbackWakeCondition;

    struct BufferedPlaybackFrame {
        AVFrame* frame = nullptr;
        double pts = 0.0;
        int64_t frameNumber = 0;

        BufferedPlaybackFrame() = default;
        ~BufferedPlaybackFrame() { av_frame_free(&frame); }
        BufferedPlaybackFrame(const BufferedPlaybackFrame&) = delete;
        BufferedPlaybackFrame& operator=(const BufferedPlaybackFrame&) = delete;
        BufferedPlaybackFrame(BufferedPlaybackFrame&& other) noexcept
            : frame(other.frame), pts(other.pts), frameNumber(other.frameNumber) {
            other.frame = nullptr;
        }
        BufferedPlaybackFrame& operator=(BufferedPlaybackFrame&& other) noexcept {
            if (this != &other) {
                av_frame_free(&frame);
                frame = other.frame;
                pts = other.pts;
                frameNumber = other.frameNumber;
                other.frame = nullptr;
            }
            return *this;
        }
    };
    std::deque<BufferedPlaybackFrame> playbackFrameBuffer;
    mutable std::mutex playbackBufferMutex;
    std::condition_variable playbackBufferCondition;
    size_t playbackBufferCapacity;
    size_t playbackPrebufferFrames;
    bool playbackDecodeEof;
    // Ordinary pause/resume keeps decoded-ahead frames so resuming does not
    // depend on a demuxer seek, which is unreliable for sparse-index inputs.
    std::atomic<bool> m_pausedPlaybackBufferResumable;

    // Audio components
    std::vector<std::unique_ptr<AudioTrack>> audioTracks;
    IMMDeviceEnumerator *deviceEnumerator;
    IMMDevice *audioDevice;
    IAudioClient *audioClient;
    IAudioRenderClient *renderClient;
    WAVEFORMATEX *audioFormat;
    UINT32 bufferFrameCount;
    bool audioInitialized;
    bool audioOutputIsFloat;
    
    // Audio threading
    std::thread audioThread;
    std::atomic<bool> audioThreadRunning;
    std::mutex audioMutex;
    std::condition_variable audioCondition;
    std::mutex decodeMutex; // protects decoder during seek
    std::mutex renderMutex; // protects frameRGB pixel buffer during sws_scale / D2D copy
    
    // Audio settings
    int audioSampleRate;
    int audioChannels;
    AVSampleFormat audioSampleFormat;

    // Shared master clock for A/V synchronization
    std::chrono::high_resolution_clock::time_point masterStartTime;
    double masterStartPts;

    // Currently loaded file path
    std::wstring loadedFilename;

    std::unique_ptr<VideoDecoder> m_decoder;
    std::unique_ptr<AudioPlayer> m_audioPlayer;
    std::unique_ptr<VideoRenderer> m_renderer;
    std::unique_ptr<VideoCutter> m_cutter;

private:
    // When true, audio packets are dropped while stepping to avoid stalls
    bool dropAudioDuringStepping;

    // True when ConsumeBwdPrefetch updated currentPts/currentFrame without
    // seeking the main formatContext (so Play() must resync before decoding).
    bool m_decoderOutOfSync;

    // DecodeNextFrame may synthesize a timestamp when a codec frame has no
    // PTS. That is adequate for continuous playback, but it cannot prove that
    // a demuxer seek actually moved to the requested part of the file.
    std::atomic<bool> m_lastDecodedFrameHadTimestamp{false};
    std::atomic<bool> m_lastReadVideoPacketHadTimestamp{false};
    std::atomic<double> m_lastReadVideoPacketPts{0.0};

    // Seeks requested while playing are consumed by the playback thread.
    // Keeping decoder ownership on that thread avoids pause/join/restart stalls.
    std::atomic<bool> m_playbackSeekPending;
    std::atomic<bool> m_playbackSeekInProgress;
    std::atomic<uint64_t> m_playbackSeekGeneration;
    std::atomic<double> m_playbackSeekTarget;
    std::atomic<bool> m_playbackSeekExact;
    // Pause can arrive before the decode thread consumes an asynchronous seek.
    // Preserve that target so the next Play cannot resume from the old/start
    // demux position.
    std::atomic<bool> m_resumeSeekPending;
    std::atomic<double> m_resumeSeekTarget;
    std::atomic<double> m_playbackSpeed;
    std::atomic<bool> m_playbackSpeedChangePending;
    std::atomic<double> m_playbackClockStartPts;
    std::atomic<int64_t> m_playbackClockStartNs;
    std::atomic<uint64_t> m_presentedPlaybackFrameCount;
    int64_t m_lastHighSpeedFrameDeliveryNs;
#ifdef VIDEO_EDITOR_TESTING
    size_t m_testPlaybackBufferCapacity = 0;
    std::atomic<bool> m_testFailNextPrimarySeek{false};
    std::atomic<uint64_t> m_testInjectedPrimarySeekFailures{0};
    std::atomic<bool> m_testNoOpNextPrimarySeek{false};
    std::atomic<uint64_t> m_testInjectedPrimarySeekNoOps{0};
    std::atomic<uint64_t> m_testFallbackSeekCount{0};
#endif
    std::atomic<ULONGLONG> m_speedOverlayDeadline;

    void ResetPlaybackClock(double pts);
    double GetPlaybackClockTarget() const;
    bool SeekDemuxer(double seconds, AVStream* stream, int64_t streamTimestamp,
                     bool skipPrimarySeek = false);

    // Background backward-frame prefetch: owns a completely separate
    // AVFormatContext so it never races with the main player.
    struct BwdPrefetch {
        std::mutex              mtx;
        std::condition_variable cv;
        bool                    exitFlag  = false;
        bool                    suspended = false;
        uint64_t                workGen   = 0;
        std::string             fileUtf8;
        double                  startOff  = 0.0;
        double                  fps       = 0.0;
        int                     sw = 0, sh = 0;

        // Current target: the background thread tries to ensure
        // we have [target - WINDOW, target] in cache.
        int64_t                 targetFrame = -1;
        // Latest frame requested by repeated ',' input. This advances even
        // when the displayed frame is waiting for the background decoder.
        int64_t                 stepTargetFrame = -1;
        int                     stepDirection = 0;

        struct ReadyFrame {
            double               pts;
            std::vector<uint8_t> pixels;
        };
        // Map of frame number -> Frame data
        std::map<int64_t, ReadyFrame> cache;
        
        std::thread             thread;
        static constexpr int    WINDOW = 60; // 2 seconds of fluid backward review - Safe RAM usage
    };
    std::unique_ptr<BwdPrefetch> m_bwdPrefetch;

    void   BwdPrefetchThreadFunc();
    void   RequestBwdPrefetch(int64_t frame);
    void   SuspendBwdPrefetch();
    bool   ConsumeBwdPrefetch(int64_t frame, bool waitForFrame = false);
    void   OnBwdFrameReady(int64_t frame);
    std::thread seekRefineThread;
    std::mutex seekRefineMutex;
    std::condition_variable seekRefineCondition;
    bool seekRefineThreadExit;
    bool seekRefinePending;
    double seekRefineTarget;
    std::uint64_t seekRefineGeneration;

    // Persistent thumbnail decoder (kept open for the lifetime of the loaded file).
    struct ThumbCtx {
        AVFormatContext* fmt  = nullptr;
        AVCodecContext*  cc   = nullptr;
        AVFrame*         frm  = nullptr;
        AVFrame*         hwFrm = nullptr;
        AVPacket*        pkt  = nullptr;
        SwsContext*      sws  = nullptr;
        int              vidIdx = -1;
        double           sto    = 0.0;  // startTimeOffset, matches main player
        int              lastDstW = 0;
        int              lastDstH = 0;
        AVPixelFormat    hwPixelFormat = AV_PIX_FMT_NONE;
        bool             useHwAccel = false;
        std::mutex       mtx;
    };
    std::unique_ptr<ThumbCtx> m_thumbCtx;
    void InitThumbnailCtx();
    void CleanupThumbnailCtx();

public:
    VideoPlayer(HWND parent);
    ~VideoPlayer();

    bool LoadVideo(const std::wstring &filename);
    void UnloadVideo();
    bool Play();
    void Pause();
    void Stop();
    void PlayClip(double startTime, double endTime);
    void PlayClips(const std::vector<ClipSegment>& segments);
    void CancelClipPreview();
    bool IsClipPreviewActive() const { return clipPreviewActive; }
    bool IsPlaying() const { return isPlaying; }
    bool IsLoaded() const { return isLoaded; }
    void SetPlaybackSpeed(double speed);
    double GetPlaybackSpeed() const { return m_playbackSpeed.load(std::memory_order_acquire); }
    bool IsPlaybackSpeedOverlayVisible() const;
    void UpdatePlaybackSpeedOverlay();

    void SeekToFrame(int64_t frameNumber);
    void StepFrame(int direction);
    bool SeekToTime(double seconds, int decodeCount = 3, bool renderFastFrame = true, bool allowAsyncRefine = true);
    void SeekToTimeExact(double seconds);  // Seek to exact timestamp for keyframe editing
    void SeekWhilePlaying(double seconds, bool exact = true);

    double GetDuration() const;
    double GetCurrentTime() const;
    int64_t GetCurrentFrame() const { return currentFrame; }
    int64_t GetTotalFrames() const { return totalFrames; }
    uint64_t GetPresentedPlaybackFrameCount() const {
        return m_presentedPlaybackFrameCount.load(std::memory_order_acquire);
    }
    size_t GetBufferedPlaybackFrameCount() const;
    bool HasPlaybackDecoderEnded() const;
#ifdef VIDEO_EDITOR_TESTING
    void ForceBufferedDecoderEagainAfterPacketsForTesting(int acceptedPackets);
    uint64_t GetInjectedBufferedDecoderEagainCountForTesting() const;
    void ForcePlaybackBufferCapacityForTesting(size_t capacity);
    void ForceNextPrimarySeekFailureForTesting();
    uint64_t GetInjectedPrimarySeekFailureCountForTesting() const;
    void ForceNextPrimarySeekNoOpForTesting();
    uint64_t GetInjectedPrimarySeekNoOpCountForTesting() const;
    uint64_t GetFallbackSeekCountForTesting() const;
    bool IsAudioOutputAvailableForTesting() const;
    uint64_t GetSubmittedAudioFrameCountForTesting() const;
    uint64_t GetAudioClientStartCountForTesting() const;
    uint64_t GetAudioClientStartFailureCountForTesting() const;
    bool IsBackwardPrefetchSuspendedForTesting();
    int GetAudioSampleRateForTesting() const { return audioSampleRate; }
#endif
    size_t GetPlaybackBufferCapacity() const { return playbackBufferCapacity; }

    // Crop timeline helpers
    bool AddCropKeyframe(double time, RECT rect, double* actualTime = nullptr);
    bool AddCropDisabledKeyframe(double time, double* actualTime = nullptr);
    bool RemoveCropKeyframe(double time);
    bool MoveCropKeyframe(double oldTime, double newTime);  // Move a keyframe to a new time
    void ClearCropKeyframes();
    bool UpdateCropForTime(double time);
    bool HasAnyCrop() const;
    bool GetCropRectForTime(double time, RECT &outRect) const;
    std::vector<double> GetCropKeyframeTimes() const;
    std::vector<CropKeyframe> GetCropKeyframes() const;
    int GetCropOutputWidth() const { return cropOutputWidth; }
    int GetCropOutputHeight() const { return cropOutputHeight; }

    void SetPosition(int x, int y, int width, int height);
    void Render();
    void ForceRedraw();

    // Decode a single frame at `time` scaled to dstW x dstH, returning BGRA pixels.
    // Opens a separate AVFormatContext so it is safe to call from a background thread.
    bool GetThumbnailPixels(double time, int dstW, int dstH, std::vector<uint8_t>& pixels) const;

    // Fast variant: reuses the persistent thumbnail decoder (avoids re-opening the file).
    // Thread-safe via its own internal mutex.
    bool GetThumbnailPixelsFast(double time, int dstW, int dstH, std::vector<uint8_t>& pixels);

    // Audio track management
    int GetAudioTrackCount() const { return static_cast<int>(audioTracks.size()); }
    std::string GetAudioTrackName(int trackIndex) const;
    bool IsAudioTrackMuted(int trackIndex) const;
    void SetAudioTrackMuted(int trackIndex, bool muted);
    float GetAudioTrackVolume(int trackIndex) const;
    void SetAudioTrackVolume(int trackIndex, float volume);
    void SetMasterVolume(float volume);
    bool IsVoiceIsolationEnabled(int trackIndex) const;
    void SetVoiceIsolationEnabled(int trackIndex, bool enabled);
    bool CutVideo(const std::wstring& outputFilename, double startTime, double endTime,
                  bool mergeAudio, bool convertH264, EncoderSelection encoder, const std::wstring& qualityPreset,
                  int maxBitrate, HWND progressBar, std::atomic<bool>* cancelFlag);
    bool CutVideo(const std::wstring& outputFilename, const std::vector<ClipSegment>& segments,
                  bool mergeAudio, bool convertH264, EncoderSelection encoder, const std::wstring& qualityPreset,
                  int maxBitrate, HWND progressBar, std::atomic<bool>* cancelFlag);

    // Timer callback
    static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR timerId, DWORD time);
    void OnTimer();

private:
    bool SeekToTimeInternal(double seconds, int decodeCount, bool allowAsyncRefine,
                            bool forceExact, bool hardSeek = false,
                            bool renderFastFrame = true, bool abortOnPendingSeek = true,
                            bool skipPrimarySeek = false,
                            bool* seekPositionValid = nullptr);
    std::uint64_t BeginSeekOperation();
    void QueueSeekRefinement(double seconds, std::uint64_t generation);
    void CancelPendingSeekRefinement();
    void SeekRefinementThreadFunction();
    void CreateVideoWindow();
    void PlaybackThreadFunction();
    void PlaybackDecodeThreadFunction();
    void ClearPlaybackBuffer();
    bool PresentBufferedFrame(BufferedPlaybackFrame&& bufferedFrame);
    static LRESULT CALLBACK VideoWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void RecomputeCropOutputDimensionsLocked();
    bool AdvanceClipPreview();
};
