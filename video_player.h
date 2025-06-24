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
}

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

  HWND parentWindow;
  HWND videoWindow;
  HDC videoDC;
  HBITMAP videoBitmap;
  BITMAPINFO bitmapInfo;

  // Timer for playback
  UINT_PTR playbackTimer;

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

  // Timer callback
  static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR timerId, DWORD time);
  void OnTimer();

private:
  bool InitializeDecoder();
  void CleanupDecoder();
  bool DecodeNextFrame();
  void UpdateDisplay();
  void CreateVideoWindow();
};
#endif // VIDEO_PLAYER_H