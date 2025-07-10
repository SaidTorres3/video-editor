#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <chrono>
#include <d2d1.h>
#pragma comment(lib, "d2d1.lib")

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
}

#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <limits>

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
    std::vector<float> buffer;
    std::vector<float> resampleBuffer;
    double nextPts;

    AudioTrack()
        : streamIndex(-1), codecContext(nullptr), swrContext(nullptr),
          frame(nullptr), isMuted(false), volume(1.0f), nextPts(-1.0) {}
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
    uint8_t *buffer;
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

    HWND parentWindow;
    HWND videoWindow;
    WNDPROC originalVideoWndProc;

    // Direct2D rendering components
    ID2D1Factory* d2dFactory;
    ID2D1HwndRenderTarget* d2dRenderTarget;
    ID2D1Bitmap* d2dBitmap;

    // Timer for playback
    UINT_PTR playbackTimer;
    // Threaded playback
    std::thread playbackThread;
    std::atomic<bool> playbackThreadRunning;

    // Playback clock for sync
    std::chrono::high_resolution_clock::time_point playClockStart;
    double playClockStartPts;

    // Audio components
    std::vector<std::unique_ptr<AudioTrack>> audioTracks;
    IMMDeviceEnumerator *deviceEnumerator;
    IMMDevice *audioDevice;
    IAudioClient *audioClient;
    IAudioRenderClient *renderClient;
    WAVEFORMATEX *audioFormat;
    UINT32 bufferFrameCount;
    bool audioInitialized;
    HANDLE audioEvent;
    
    // Audio threading
    std::thread audioThread;
    std::atomic<bool> audioThreadRunning;
    std::mutex audioMutex;
    std::mutex decodeMutex; // protects decoder during seek
    
    // Audio settings
    int audioSampleRate;
    int audioChannels;
    AVSampleFormat audioSampleFormat;

    // Currently loaded file path
    std::wstring loadedFilename;

    std::unique_ptr<VideoDecoder> m_decoder;
    std::unique_ptr<AudioPlayer> m_audioPlayer;
    std::unique_ptr<VideoRenderer> m_renderer;
    std::unique_ptr<VideoCutter> m_cutter;

private:

public:
    VideoPlayer(HWND parent);
    ~VideoPlayer();

    bool LoadVideo(const std::wstring &filename);
    void UnloadVideo();
    bool Play();
    void Pause();
    void Stop();
    bool IsPlaying() const { return isPlaying; }
    bool IsLoaded() const { return isLoaded; }

    void SeekToFrame(int64_t frameNumber);
    void SeekToTime(double seconds);

    double GetDuration() const;
    double GetCurrentTime() const;
    double GetSyncTime() const;
    int64_t GetCurrentFrame() const { return currentFrame; }
    int64_t GetTotalFrames() const { return totalFrames; }

    void SetPosition(int x, int y, int width, int height);
    void Render();

    // Audio track management
    int GetAudioTrackCount() const { return static_cast<int>(audioTracks.size()); }
    std::string GetAudioTrackName(int trackIndex) const;
    bool IsAudioTrackMuted(int trackIndex) const;
    void SetAudioTrackMuted(int trackIndex, bool muted);
    float GetAudioTrackVolume(int trackIndex) const;
    void SetAudioTrackVolume(int trackIndex, float volume);
    void SetMasterVolume(float volume);
    bool CutVideo(const std::wstring& outputFilename, double startTime, double endTime,
                  bool mergeAudio, bool convertH264, bool useNvenc,
                  int maxBitrate, HWND progressBar, std::atomic<bool>* cancelFlag);

    // Timer callback
    static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR timerId, DWORD time);
    void OnTimer();

private:
    void CreateVideoWindow();
    void PlaybackThreadFunction();
    static LRESULT CALLBACK VideoWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};