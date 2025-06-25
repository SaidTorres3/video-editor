#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include <windows.h>
#include <string>

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
#include <condition_variable>
#include <queue>
#include <atomic>

// Audio output using Windows Audio Session API (WASAPI)
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>

// Audio track structure
struct AudioTrack {
    int streamIndex;
    AVCodecContext *codecContext;
    SwrContext *swrContext;
    AVFrame *frame;
    bool isMuted;
    float volume;
    std::string name;
    
    AudioTrack() : streamIndex(-1), codecContext(nullptr), swrContext(nullptr),
                   frame(nullptr), isMuted(false), volume(1.0f) {}
};

class VideoPlayer
{
private:
  AVFormatContext *formatContext;
  AVCodecContext *codecContext;
  AVFrame *frame;
  AVFrame *frameRGB;
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

  HWND parentWindow;
  HWND videoWindow;
  HDC videoDC;
  HBITMAP videoBitmap;
  BITMAPINFO bitmapInfo;

  // Timer for playback
  UINT_PTR playbackTimer;
  // Threaded playback
  std::thread playbackThread;
  std::atomic<bool> playbackThreadRunning;

  // Audio components
  std::vector<std::unique_ptr<AudioTrack>> audioTracks;
  IMMDeviceEnumerator *deviceEnumerator;
  IMMDevice *audioDevice;
  IAudioClient *audioClient;
  IAudioRenderClient *renderClient;
  WAVEFORMATEX *audioFormat;
  UINT32 bufferFrameCount;
  bool audioInitialized;
  
  // Audio threading
  std::thread audioThread;
  std::atomic<bool> audioThreadRunning;
  std::mutex audioMutex;
  std::condition_variable audioCondition;
  std::queue<std::vector<uint8_t>> audioQueue;
  
  // Audio settings
  int audioSampleRate;
  int audioChannels;
  AVSampleFormat audioSampleFormat;

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
  bool CutVideo(const std::wstring& outputFilename, double startTime, double endTime, bool mergeAudio);

  // Timer callback
  static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR timerId, DWORD time);
  void OnTimer();

private:
  bool InitializeDecoder();
  void CleanupDecoder();
  bool DecodeNextFrame();
  void UpdateDisplay();
  void CreateVideoWindow();
  
  // Audio methods
  bool InitializeAudio();
  void CleanupAudio();
  bool InitializeAudioTracks();
  void CleanupAudioTracks();
  void AudioThreadFunction();
  void PlaybackThreadFunction();
  bool ProcessAudioFrame(AVPacket *audioPacket);
  void MixAudioTracks(uint8_t *outputBuffer, int frameCount);
};
#endif // VIDEO_PLAYER_H