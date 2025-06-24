// video_player.cpp
#include "video_player.h"
#include <iostream>

VideoPlayer::VideoPlayer(HWND parent)
    : parentWindow(parent), formatContext(nullptr), codecContext(nullptr),
      frame(nullptr), frameRGB(nullptr), packet(nullptr), swsContext(nullptr),
      buffer(nullptr), videoStreamIndex(-1), frameWidth(0), frameHeight(0),
      isLoaded(false), isPlaying(false), frameRate(0), currentFrame(0),
      totalFrames(0), videoWindow(nullptr), videoDC(nullptr),
      videoBitmap(nullptr), playbackTimer(0)
{
  CreateVideoWindow();
}

VideoPlayer::~VideoPlayer()
{
  UnloadVideo();
  if (videoWindow)
    DestroyWindow(videoWindow);
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
    videoDC = GetDC(videoWindow);
}

bool VideoPlayer::LoadVideo(const std::wstring &filename)
{
  UnloadVideo();

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

  if (!InitializeDecoder())
  {
    UnloadVideo();
    return false;
  }

  isLoaded = true;
  currentFrame = 0;
  AVStream *vs = formatContext->streams[videoStreamIndex];
  frameRate = av_q2d(vs->r_frame_rate);
  totalFrames = vs->nb_frames
                    ? vs->nb_frames
                    : (vs->duration != AV_NOPTS_VALUE
                           ? (int64_t)(av_q2d(vs->time_base) * vs->duration * frameRate)
                           : 0);
  return true;
}

bool VideoPlayer::InitializeDecoder()
{
  AVStream *vs = formatContext->streams[videoStreamIndex];
  AVCodecParameters *cp = vs->codecpar;
  const AVCodec *codec = avcodec_find_decoder(cp->codec_id);
  if (!codec)
    return false;

  codecContext = avcodec_alloc_context3(codec);
  if (!codecContext)
    return false;
  if (avcodec_parameters_to_context(codecContext, cp) < 0)
  {
    avcodec_free_context(&codecContext);
    return false;
  }
  if (avcodec_open2(codecContext, codec, nullptr) < 0)
  {
    avcodec_free_context(&codecContext);
    return false;
  }

  frameWidth = codecContext->width;
  frameHeight = codecContext->height;
  frame = av_frame_alloc();
  frameRGB = av_frame_alloc();
  packet = av_packet_alloc();
  if (!frame || !frameRGB || !packet)
  {
    CleanupDecoder();
    return false;
  }

  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, frameWidth, frameHeight, 32);
  buffer = (uint8_t *)av_malloc(numBytes);
  av_image_fill_arrays(frameRGB->data, frameRGB->linesize, buffer,
                       AV_PIX_FMT_BGR24, frameWidth, frameHeight, 32);

  swsContext = sws_getContext(
      frameWidth, frameHeight, codecContext->pix_fmt,
      frameWidth, frameHeight, AV_PIX_FMT_BGR24,
      SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!swsContext)
  {
    CleanupDecoder();
    return false;
  }

  ZeroMemory(&bitmapInfo, sizeof(bitmapInfo));
  bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmapInfo.bmiHeader.biWidth = frameWidth;
  bitmapInfo.bmiHeader.biHeight = -frameHeight; // top-down
  bitmapInfo.bmiHeader.biPlanes = 1;
  bitmapInfo.bmiHeader.biBitCount = 24;
  bitmapInfo.bmiHeader.biCompression = BI_RGB;

  return true;
}

void VideoPlayer::CleanupDecoder()
{
  if (swsContext)
    sws_freeContext(swsContext), swsContext = nullptr;
  if (buffer)
    av_free(buffer), buffer = nullptr;
  if (packet)
    av_packet_free(&packet), packet = nullptr;
  if (frameRGB)
    av_frame_free(&frameRGB), frameRGB = nullptr;
  if (frame)
    av_frame_free(&frame), frame = nullptr;
  if (codecContext)
    avcodec_free_context(&codecContext), codecContext = nullptr;
  if (videoBitmap)
    DeleteObject(videoBitmap), videoBitmap = nullptr;
}

void VideoPlayer::UnloadVideo()
{
  Stop();
  CleanupDecoder();
  if (formatContext)
  {
    avformat_close_input(&formatContext);
    formatContext = nullptr;
  }
  isLoaded = false;
  videoStreamIndex = -1;
  currentFrame = 0;
  totalFrames = 0;
}

bool VideoPlayer::Play()
{
  if (!isLoaded || isPlaying)
    return false;
  isPlaying = true;
  int interval = (int)(1000.0 / (frameRate > 0 ? frameRate : 30.0));
  playbackTimer = SetTimer(parentWindow, 1, interval, TimerProc);
  return true;
}

void VideoPlayer::Pause()
{
  if (isPlaying)
  {
    isPlaying = false;
    if (playbackTimer)
    {
      KillTimer(parentWindow, playbackTimer);
      playbackTimer = 0;
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
  }
}

bool VideoPlayer::DecodeNextFrame()
{
  if (!isLoaded)
    return false;
  int ret = av_read_frame(formatContext, packet);
  if (ret < 0)
  {
    Stop();
    return false;
  }

  if (packet->stream_index == videoStreamIndex)
  {
    ret = avcodec_send_packet(codecContext, packet);
    av_packet_unref(packet);
    if (ret < 0)
      return false;
    ret = avcodec_receive_frame(codecContext, frame);
    if (ret == 0)
    {
      sws_scale(
          swsContext,
          (uint8_t const *const *)frame->data, frame->linesize,
          0, frameHeight,
          frameRGB->data, frameRGB->linesize);
      currentFrame++;
      UpdateDisplay();
      return true;
    }
  }
  return false;
}

void VideoPlayer::UpdateDisplay()
{
  if (!videoDC || !frameRGB->data[0])
    return;

  RECT rect;
  GetClientRect(videoWindow, &rect);
  int winW = rect.right - rect.left;
  int winH = rect.bottom - rect.top;

  StretchDIBits(
      videoDC,
      0, 0, winW, winH,
      0, 0, frameWidth, frameHeight,
      frameRGB->data[0],
      &bitmapInfo,
      DIB_RGB_COLORS,
      SRCCOPY);
}

void VideoPlayer::SeekToFrame(int64_t frameNumber)
{
  if (!isLoaded || frameNumber < 0 || frameNumber >= totalFrames)
    return;
  AVStream *vs = formatContext->streams[videoStreamIndex];
  int64_t ts = av_rescale_q(frameNumber, av_inv_q(vs->r_frame_rate), vs->time_base);
  av_seek_frame(formatContext, videoStreamIndex, ts, AVSEEK_FLAG_FRAME);
  avcodec_flush_buffers(codecContext);
  currentFrame = frameNumber;
  DecodeNextFrame();
}

void VideoPlayer::SeekToTime(double seconds)
{
  if (frameRate > 0)
    SeekToFrame((int64_t)(seconds * frameRate));
}

double VideoPlayer::GetDuration() const
{
  if (!isLoaded)
    return 0.0;
  AVStream *vs = formatContext->streams[videoStreamIndex];
  if (vs->duration != AV_NOPTS_VALUE)
    return av_q2d(vs->time_base) * vs->duration;
  if (totalFrames > 0 && frameRate > 0)
    return totalFrames / frameRate;
  return 0.0;
}

double VideoPlayer::GetCurrentTime() const
{
  return frameRate > 0 ? (currentFrame / frameRate) : 0.0;
}

// — Nota: NO incluimos aquí GetCurrentFrame() ni GetTotalFrames()
//    porque ya están definidas inline en video_player.h

void VideoPlayer::SetPosition(int x, int y, int width, int height)
{
  if (!videoWindow)
    return;
  SetWindowPos(videoWindow, nullptr, x, y, width, height, SWP_NOZORDER);
  InvalidateRect(videoWindow, nullptr, TRUE);
  UpdateWindow(videoWindow);
}

void VideoPlayer::Render()
{
  if (isLoaded && !isPlaying)
    DecodeNextFrame();
}

void CALLBACK VideoPlayer::TimerProc(HWND hwnd, UINT, UINT_PTR, DWORD)
{
  VideoPlayer *player = (VideoPlayer *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
  if (player && player->isPlaying)
    player->DecodeNextFrame();
}

void VideoPlayer::OnTimer()
{
  if (isPlaying)
    DecodeNextFrame();
}
