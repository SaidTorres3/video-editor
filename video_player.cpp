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

  // Initialize FFmpeg

  CreateVideoWindow();
}

VideoPlayer::~VideoPlayer()
{
  UnloadVideo();
  if (videoWindow)
  {
    DestroyWindow(videoWindow);
  }
}

void VideoPlayer::CreateVideoWindow()
{
  videoWindow = CreateWindow(
      L"STATIC", L"",
      WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
      10, 10, 640, 480,
      parentWindow, nullptr,
      (HINSTANCE)GetWindowLongPtr(parentWindow, GWLP_HINSTANCE),
      nullptr);

  if (videoWindow)
  {
    videoDC = GetDC(videoWindow);
  }
}

bool VideoPlayer::LoadVideo(const std::wstring &filename)
{
  UnloadVideo();

  // Convert wstring to string (UTF-8) for FFmpeg
  int bufferSize = WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string utf8Filename(bufferSize, 0);
  WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, &utf8Filename[0], bufferSize, nullptr, nullptr);

  formatContext = avformat_alloc_context();
  if (avformat_open_input(&formatContext, utf8Filename.c_str(), nullptr, nullptr) < 0)
  {
    return false;
  }

  if (avformat_find_stream_info(formatContext, nullptr) < 0)
  {
    avformat_close_input(&formatContext);
    return false;
  }

  // Find video stream
  videoStreamIndex = -1;
  for (unsigned int i = 0; i < formatContext->nb_streams; i++)
  {
    if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
      videoStreamIndex = i;
      break;
    }
  }

  if (videoStreamIndex == -1)
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

  // Calculate total frames and frame rate
  AVStream *videoStream = formatContext->streams[videoStreamIndex];
  frameRate = av_q2d(videoStream->r_frame_rate);
  totalFrames = videoStream->nb_frames;

  if (totalFrames == 0)
  {
    // Estimate from duration if nb_frames is not available
    if (videoStream->duration != AV_NOPTS_VALUE)
    {
      totalFrames = (int64_t)(av_q2d(videoStream->time_base) * videoStream->duration * frameRate);
    }
  }

  return true;
}

bool VideoPlayer::InitializeDecoder()
{
  AVStream *videoStream = formatContext->streams[videoStreamIndex];
  AVCodecParameters *codecParams = videoStream->codecpar;

  const AVCodec *codec = avcodec_find_decoder(codecParams->codec_id);
  if (!codec)
  {
    return false;
  }

  codecContext = avcodec_alloc_context3(codec);
  if (!codecContext)
  {
    return false;
  }

  if (avcodec_parameters_to_context(codecContext, codecParams) < 0)
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

  // Allocate buffer for RGB frame
  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGR24, frameWidth, frameHeight, 32);
  buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));

  av_image_fill_arrays(frameRGB->data, frameRGB->linesize, buffer,
                       AV_PIX_FMT_BGR24, frameWidth, frameHeight, 32);

  // Initialize scaling context
  swsContext = sws_getContext(frameWidth, frameHeight, codecContext->pix_fmt,
                              frameWidth, frameHeight, AV_PIX_FMT_BGR24,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);

  if (!swsContext)
  {
    CleanupDecoder();
    return false;
  }

  // Setup bitmap info for Windows
  ZeroMemory(&bitmapInfo, sizeof(BITMAPINFO));
  bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmapInfo.bmiHeader.biWidth = frameWidth;
  bitmapInfo.bmiHeader.biHeight = -frameHeight; // Negative for top-down
  bitmapInfo.bmiHeader.biPlanes = 1;
  bitmapInfo.bmiHeader.biBitCount = 24;
  bitmapInfo.bmiHeader.biCompression = BI_RGB;

  return true;
}

void VideoPlayer::CleanupDecoder()
{
  if (swsContext)
  {
    sws_freeContext(swsContext);
    swsContext = nullptr;
  }

  if (buffer)
  {
    av_free(buffer);
    buffer = nullptr;
  }

  if (packet)
  {
    av_packet_free(&packet);
    packet = nullptr;
  }

  if (frameRGB)
  {
    av_frame_free(&frameRGB);
    frameRGB = nullptr;
  }

  if (frame)
  {
    av_frame_free(&frame);
    frame = nullptr;
  }

  if (codecContext)
  {
    avcodec_free_context(&codecContext);
    codecContext = nullptr;
  }

  if (videoBitmap)
  {
    DeleteObject(videoBitmap);
    videoBitmap = nullptr;
  }
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
  {
    return false;
  }

  isPlaying = true;

  // Set up timer for playback (assuming 30 FPS for now)
  int timerInterval = (int)(1000.0 / (frameRate > 0 ? frameRate : 30.0));
  playbackTimer = SetTimer(parentWindow, 1, timerInterval, TimerProc);

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
    // End of file or error
    Stop();
    return false;
  }

  if (packet->stream_index == videoStreamIndex)
  {
    ret = avcodec_send_packet(codecContext, packet);
    if (ret < 0)
    {
      av_packet_unref(packet);
      return false;
    }

    ret = avcodec_receive_frame(codecContext, frame);
    if (ret == 0)
    {
      // Convert to RGB
      sws_scale(swsContext, (uint8_t const *const *)frame->data,
                frame->linesize, 0, frameHeight,
                frameRGB->data, frameRGB->linesize);

      currentFrame++;
      UpdateDisplay();
      av_packet_unref(packet);
      return true;
    }
  }

  av_packet_unref(packet);
  return false;
}

void VideoPlayer::UpdateDisplay()
{
  if (!videoDC || !frameRGB->data[0])
    return;

  SetDIBitsToDevice(videoDC,
                    0, 0, frameWidth, frameHeight,
                    0, 0, 0, frameHeight,
                    frameRGB->data[0], &bitmapInfo, DIB_RGB_COLORS);
}

void VideoPlayer::SeekToFrame(int64_t frameNumber)
{
  if (!isLoaded || frameNumber < 0 || frameNumber >= totalFrames)
    return;

  AVStream *videoStream = formatContext->streams[videoStreamIndex];
  int64_t timestamp = av_rescale_q(frameNumber, av_inv_q(videoStream->r_frame_rate), videoStream->time_base);

  av_seek_frame(formatContext, videoStreamIndex, timestamp, AVSEEK_FLAG_FRAME);
  avcodec_flush_buffers(codecContext);

  currentFrame = frameNumber;
  DecodeNextFrame();
}

void VideoPlayer::SeekToTime(double seconds)
{
  if (frameRate > 0)
  {
    SeekToFrame((int64_t)(seconds * frameRate));
  }
}

double VideoPlayer::GetDuration() const
{
  if (!isLoaded)
    return 0.0;

  AVStream *videoStream = formatContext->streams[videoStreamIndex];
  if (videoStream->duration != AV_NOPTS_VALUE)
  {
    return av_q2d(videoStream->time_base) * videoStream->duration;
  }

  if (totalFrames > 0 && frameRate > 0)
  {
    return totalFrames / frameRate;
  }

  return 0.0;
}

double VideoPlayer::GetCurrentTime() const
{
  if (frameRate > 0)
  {
    return currentFrame / frameRate;
  }
  return 0.0;
}

void VideoPlayer::SetPosition(int x, int y, int width, int height)
{
  if (videoWindow)
  {
    SetWindowPos(videoWindow, nullptr, x, y, width, height, SWP_NOZORDER);
  }
}

void VideoPlayer::Render()
{
  if (isLoaded && !isPlaying)
  {
    DecodeNextFrame();
  }
}

void CALLBACK VideoPlayer::TimerProc(HWND hwnd, UINT msg, UINT_PTR timerId, DWORD time)
{
  // Find the VideoPlayer instance associated with this window
  VideoPlayer *player = (VideoPlayer *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
  if (player)
  {
    player->OnTimer();
  }
}

void VideoPlayer::OnTimer()
{
  if (isPlaying)
  {
    DecodeNextFrame();
  }
}