// video_player.cpp
#include "video_player.h"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <windows.h>
#include <fstream>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dxgiformat.h>
#pragma comment(lib, "d2d1.lib")
#include <uxtheme.h>
#include <algorithm>
#include <cstring>
#include <chrono>

// Simple debug logging helper that also writes to a file and can show popups
static std::ofstream g_debugFile;
static void DebugLog(const std::string& msg, bool popup = false)
{
    if (!g_debugFile.is_open())
        g_debugFile.open("debug.log", std::ios::app);
    if (g_debugFile.is_open())
        g_debugFile << msg << std::endl;

    OutputDebugStringA((msg + "\n").c_str());
    if (popup)
        MessageBoxA(nullptr, msg.c_str(), "Video Editor Debug", MB_OK | MB_ICONINFORMATION);
}

// Link with Windows Audio libraries
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

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
  InitializeD2D();
  CreateVideoWindow();
  InitializeAudio();
}

VideoPlayer::~VideoPlayer()
{
  UnloadVideo();
  CleanupAudio();
  CleanupD2D();
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
    CreateRenderTarget();
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

  if (!InitializeDecoder())
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
  if (!InitializeAudioTracks())
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

bool VideoPlayer::InitializeDecoder()
{
  AVStream *vs = formatContext->streams[videoStreamIndex];
  AVCodecParameters *cp = vs->codecpar;
  const AVCodec *codec = nullptr;
  useHwAccel = false;

  if (cp->codec_id == AV_CODEC_ID_H264)
  {
    codec = avcodec_find_decoder_by_name("h264_dxva2");
    if (codec)
      useHwAccel = true;
  }
  if (!codec)
    codec = avcodec_find_decoder(cp->codec_id);
  if (!codec)
    return false;

  codecContext = avcodec_alloc_context3(codec);
  if (!codecContext)
    return false;
  codecContext->opaque = this;
  codecContext->get_format = [](AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    VideoPlayer* vp = reinterpret_cast<VideoPlayer*>(ctx->opaque);
    for (const enum AVPixelFormat *p = pix_fmts; *p != -1; p++) {
      if (*p == AV_PIX_FMT_DXVA2_VLD) {
        vp->hwPixelFormat = *p;
        return *p;
      }
    }
    vp->hwPixelFormat = pix_fmts[0];
    return pix_fmts[0];
  };

  if (avcodec_parameters_to_context(codecContext, cp) < 0)
  {
    avcodec_free_context(&codecContext);
    return false;
  }

  // Skip non-reference frames to decode faster at the cost of quality
  codecContext->skip_frame = AVDISCARD_NONREF;

  if (useHwAccel)
  {
    if (av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_DXVA2, nullptr, nullptr, 0) < 0)
    {
      useHwAccel = false;
    }
    else
    {
      codecContext->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
    }
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
  hwFrame = av_frame_alloc();
  packet = av_packet_alloc();
  if (!frame || !frameRGB || !hwFrame || !packet)
  {
    CleanupDecoder();
    return false;
  }

  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, frameWidth, frameHeight, 32);
  buffer = (uint8_t *)av_malloc(numBytes);
  av_image_fill_arrays(frameRGB->data, frameRGB->linesize, buffer,
                       AV_PIX_FMT_BGRA, frameWidth, frameHeight, 32);

  enum AVPixelFormat swFmt = codecContext->sw_pix_fmt != AV_PIX_FMT_NONE ?
                            codecContext->sw_pix_fmt : codecContext->pix_fmt;
  swsContext = sws_getContext(
      frameWidth, frameHeight, swFmt,
      frameWidth, frameHeight, AV_PIX_FMT_BGRA,
      SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
  if (!swsContext)
  {
    CleanupDecoder();
    return false;
  }

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
  if (hwFrame)
    av_frame_free(&hwFrame), hwFrame = nullptr;
  if (frame)
    av_frame_free(&frame), frame = nullptr;
  if (codecContext)
    avcodec_free_context(&codecContext), codecContext = nullptr;
  if (hwDeviceCtx)
    av_buffer_unref(&hwDeviceCtx), hwDeviceCtx = nullptr;
  useHwAccel = false;
}

void VideoPlayer::UnloadVideo()
{
  Stop();
  CleanupAudioTracks();
  CleanupDecoder();
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
  
  // Start audio thread if we have audio tracks
  if (!audioTracks.empty() && audioInitialized)
  {
    HRESULT hr = audioClient->Start();
    if (FAILED(hr))
    {
      std::cerr << "Failed to start audio client: " << std::hex << hr << std::endl;
      // Continue without audio or handle error appropriately
    }
    audioThreadRunning = true;
    audioThread = std::thread(&VideoPlayer::AudioThreadFunction, this);
  }
  playbackThreadRunning = true;
  playbackThread = std::thread(&VideoPlayer::PlaybackThreadFunction, this);
  return true;
}

void VideoPlayer::Pause()
{
  if (isPlaying)
  {
    isPlaying = false;
    
    // Stop audio thread
    if (audioThreadRunning)
    {
      audioThreadRunning = false;
      audioCondition.notify_all();
      if (audioThread.joinable())
        audioThread.join();
    }
    
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

bool VideoPlayer::DecodeNextFrame(bool updateDisplay)
{
  if (!isLoaded)
    return false;

  std::lock_guard<std::mutex> lock(decodeMutex);

  while (true)
  {
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
        continue;

      while (true)
      {
        ret = avcodec_receive_frame(codecContext, hwFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
          break;
        if (ret < 0)
          return false;

        AVFrame* swFrame = hwFrame;
        if (useHwAccel && hwFrame->format == hwPixelFormat)
        {
          if (av_hwframe_transfer_data(frame, hwFrame, 0) < 0)
            return false;
          swFrame = frame;
        }

        AVStream *vs = formatContext->streams[videoStreamIndex];
        double pts = 0.0;
        if (swFrame->best_effort_timestamp != AV_NOPTS_VALUE)
          pts = swFrame->best_effort_timestamp * av_q2d(vs->time_base);
        else if (swFrame->pts != AV_NOPTS_VALUE)
          pts = swFrame->pts * av_q2d(vs->time_base);
        else
          pts = currentPts + (frameRate > 0 ? 1.0 / frameRate : 0.0);
        currentPts = pts - startTimeOffset;
        if (currentPts < 0.0)
          currentPts = 0.0;
        currentFrame++;
        sws_scale(
            swsContext,
            (uint8_t const *const *)swFrame->data, swFrame->linesize,
            0, frameHeight,
            frameRGB->data, frameRGB->linesize);
        if (updateDisplay)
        {
          UpdateDisplay();
        }
        else
        {
          InvalidateRect(videoWindow, nullptr, FALSE);
        }

        av_frame_unref(hwFrame);
        if (swFrame != hwFrame)
          av_frame_unref(swFrame);
        return true;
      }
    }
    else
    {
      // Check if this is an audio packet
      for (auto& track : audioTracks)
      {
        if (packet->stream_index == track->streamIndex)
        {
          ProcessAudioFrame(packet);
          break;
        }
      }
      av_packet_unref(packet);
    }
  }
  return false; // Should never reach here
}

void VideoPlayer::UpdateDisplay()
{
  if (!d2dRenderTarget || !frameRGB->data[0])
    return;

  std::lock_guard<std::mutex> lock(decodeMutex);

  if (!d2dBitmap)
  {
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    d2dRenderTarget->CreateBitmap(
        D2D1::SizeU(frameWidth, frameHeight),
        frameRGB->data[0],
        frameRGB->linesize[0],
        props,
        &d2dBitmap);
  }
  else
  {
    D2D1_RECT_U rect = {0, 0, (UINT32)frameWidth, (UINT32)frameHeight};
    d2dBitmap->CopyFromMemory(&rect, frameRGB->data[0], frameRGB->linesize[0]);
  }

  d2dRenderTarget->BeginDraw();
  d2dRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));
  D2D1_SIZE_F size = d2dRenderTarget->GetSize();
  float targetAspect = size.width / size.height;
  float videoAspect = static_cast<float>(frameWidth) / frameHeight;
  float drawWidth = size.width;
  float drawHeight = size.height;
  float offsetX = 0.0f;
  float offsetY = 0.0f;
  if (targetAspect > videoAspect)
  {
    drawHeight = size.height;
    drawWidth = drawHeight * videoAspect;
    offsetX = (size.width - drawWidth) / 2.0f;
  }
  else
  {
    drawWidth = size.width;
    drawHeight = drawWidth / videoAspect;
    offsetY = (size.height - drawHeight) / 2.0f;
  }

  d2dRenderTarget->DrawBitmap(
      d2dBitmap,
      D2D1::RectF(offsetX, offsetY, offsetX + drawWidth, offsetY + drawHeight),
      1.0f,
      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
  d2dRenderTarget->EndDraw();
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
    if (!DecodeNextFrame(true))
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
  if (!videoWindow)
    return;
  SetWindowPos(videoWindow, nullptr, x, y, width, height, SWP_NOZORDER);
  if (d2dRenderTarget)
  {
    d2dRenderTarget->Resize(D2D1::SizeU(width, height));
  }
  InvalidateRect(videoWindow, nullptr, TRUE);
  UpdateWindow(videoWindow);
}

void VideoPlayer::Render()
{
  if (isLoaded && !isPlaying)
    DecodeNextFrame(true);
}

void CALLBACK VideoPlayer::TimerProc(HWND hwnd, UINT, UINT_PTR, DWORD)
{
  VideoPlayer *player = (VideoPlayer *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
  if (player && player->isPlaying)
    player->DecodeNextFrame(true);
}

void VideoPlayer::OnTimer()
{
  if (isPlaying)
    DecodeNextFrame(true);
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
  float clampedVolume = volume < 0.0f ? 0.0f : (volume > 2.0f ? 2.0f : volume);
  for (auto& track : audioTracks)
  {
    track->volume = clampedVolume;
  }
}

// Audio initialization and cleanup
bool VideoPlayer::InitializeAudio()
{
  HRESULT hr = CoInitialize(nullptr);
  if (FAILED(hr))
    return false;

  hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                        __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator);
  if (FAILED(hr))
    return false;

  hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &audioDevice);
  if (FAILED(hr))
    return false;

  hr = audioDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
  if (FAILED(hr))
    return false;

  // Get the default audio format
  WAVEFORMATEX *deviceFormat = nullptr;
  hr = audioClient->GetMixFormat(&deviceFormat);
  if (FAILED(hr))
    return false;

  // Set up our desired format (16-bit stereo at 44.1kHz)
  audioFormat = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
  audioFormat->wFormatTag = WAVE_FORMAT_PCM;
  audioFormat->nChannels = 2;
  audioFormat->nSamplesPerSec = 44100;
  audioFormat->wBitsPerSample = 16;
  audioFormat->nBlockAlign = (audioFormat->nChannels * audioFormat->wBitsPerSample) / 8;
  audioFormat->nAvgBytesPerSec = audioFormat->nSamplesPerSec * audioFormat->nBlockAlign;
  audioFormat->cbSize = 0;

  hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, audioFormat, nullptr);
  if (FAILED(hr))
  {
    // Try with device format if our format fails
    CoTaskMemFree(audioFormat);
    audioFormat = deviceFormat;
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, audioFormat, nullptr);
    if (FAILED(hr))
      return false;
  }
  else
  {
    CoTaskMemFree(deviceFormat);
  }

  // Update audio configuration to match the initialized format
  audioSampleRate = audioFormat->nSamplesPerSec;
  audioChannels = audioFormat->nChannels;

  hr = audioClient->GetBufferSize(&bufferFrameCount);
  if (FAILED(hr))
    return false;

  hr = audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&renderClient);
  if (FAILED(hr))
    return false;

  audioInitialized = true;
  return true;
}

void VideoPlayer::CleanupAudio()
{
  if (audioThreadRunning)
  {
    audioThreadRunning = false;
    audioCondition.notify_all();
    if (audioThread.joinable())
      audioThread.join();
  }

  if (renderClient)
  {
    renderClient->Release();
    renderClient = nullptr;
  }
  if (audioClient)
  {
    audioClient->Release();
    audioClient = nullptr;
  }
  if (audioDevice)
  {
    audioDevice->Release();
    audioDevice = nullptr;
  }
  if (deviceEnumerator)
  {
    deviceEnumerator->Release();
    deviceEnumerator = nullptr;
  }
  if (audioFormat)
  {
    CoTaskMemFree(audioFormat);
    audioFormat = nullptr;
  }
  
  audioInitialized = false;
  CoUninitialize();
}

bool VideoPlayer::InitializeAudioTracks()
{
  if (!formatContext)
    return false;

  // Find all audio streams
  for (unsigned i = 0; i < formatContext->nb_streams; i++)
  {
    if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    {
      auto track = std::make_unique<AudioTrack>();
      track->streamIndex = i;
      
      // Get codec and create context
      AVCodecParameters *codecpar = formatContext->streams[i]->codecpar;
      const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
      if (!codec)
        continue;

      track->codecContext = avcodec_alloc_context3(codec);
      if (!track->codecContext)
        continue;

      if (avcodec_parameters_to_context(track->codecContext, codecpar) < 0)
      {
        avcodec_free_context(&track->codecContext);
        continue;
      }

      if (avcodec_open2(track->codecContext, codec, nullptr) < 0)
      {
        avcodec_free_context(&track->codecContext);
        continue;
      }

      // Allocate frame
      track->frame = av_frame_alloc();
      if (!track->frame)
      {
        avcodec_free_context(&track->codecContext);
        continue;
      }

      // Set up resampler
      track->swrContext = swr_alloc();
      if (!track->swrContext)
      {
        av_frame_free(&track->frame);
        avcodec_free_context(&track->codecContext);
        continue;
      }

      // Configure resampler to convert to our output format
      AVChannelLayout in_ch_layout, out_ch_layout;
      av_channel_layout_from_mask(&in_ch_layout, track->codecContext->ch_layout.nb_channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO);
      av_channel_layout_from_mask(&out_ch_layout, AV_CH_LAYOUT_STEREO);
      
      av_opt_set_chlayout(track->swrContext, "in_chlayout", &in_ch_layout, 0);
      av_opt_set_chlayout(track->swrContext, "out_chlayout", &out_ch_layout, 0);
      av_opt_set_int(track->swrContext, "in_sample_rate", track->codecContext->sample_rate, 0);
      av_opt_set_int(track->swrContext, "out_sample_rate", audioSampleRate, 0);
      av_opt_set_sample_fmt(track->swrContext, "in_sample_fmt", track->codecContext->sample_fmt, 0);
      av_opt_set_sample_fmt(track->swrContext, "out_sample_fmt", audioSampleFormat, 0);

      if (swr_init(track->swrContext) < 0)
      {
        swr_free(&track->swrContext);
        av_frame_free(&track->frame);
        avcodec_free_context(&track->codecContext);
        continue;
      }

      // Set track name
      AVDictionaryEntry *title = av_dict_get(formatContext->streams[i]->metadata, "title", nullptr, 0);
      if (title)
        track->name = title->value;
      else
        track->name = "Audio Track " + std::to_string(audioTracks.size() + 1);

      audioTracks.push_back(std::move(track));
    }
  }

  return !audioTracks.empty();
}

void VideoPlayer::CleanupAudioTracks()
{
  for (auto& track : audioTracks)
  {
    if (track->swrContext)
      swr_free(&track->swrContext);
    if (track->frame)
      av_frame_free(&track->frame);
    if (track->codecContext)
      avcodec_free_context(&track->codecContext);
  }
  audioTracks.clear();
}

bool VideoPlayer::ProcessAudioFrame(AVPacket *audioPacket)
{
  if (!audioInitialized || audioTracks.empty())
    return false;

  // Find the corresponding audio track
  AudioTrack *track = nullptr;
  for (auto& t : audioTracks)
  {
    if (t->streamIndex == audioPacket->stream_index)
    {
      track = t.get();
      break;
    }
  }

  if (!track || track->isMuted)
    return false;

  // Decode audio frame
  int ret = avcodec_send_packet(track->codecContext, audioPacket);
  if (ret < 0)
    return false;

  ret = avcodec_receive_frame(track->codecContext, track->frame);
  if (ret < 0)
    return false;

  AVStream *as = formatContext->streams[track->streamIndex];
  double framePts = 0.0;
  if (track->frame->best_effort_timestamp != AV_NOPTS_VALUE)
    framePts = track->frame->best_effort_timestamp * av_q2d(as->time_base);
  else if (track->frame->pts != AV_NOPTS_VALUE)
    framePts = track->frame->pts * av_q2d(as->time_base);
  if (framePts - startTimeOffset < 0.0)
    return true; // Drop early audio

  // Resample audio
  int outSamples = swr_get_out_samples(track->swrContext, track->frame->nb_samples);
  size_t needed = static_cast<size_t>(outSamples * audioChannels);
  if (track->resampleBuffer.size() < needed)
      track->resampleBuffer.resize(needed);
  int16_t* outPtr = track->resampleBuffer.data();

  int convertedSamples = swr_convert(track->swrContext, (uint8_t**)&outPtr, outSamples,
                                    (const uint8_t**)track->frame->data, track->frame->nb_samples);
  if (convertedSamples < 0)
    return false;

  // Store raw samples in track buffer
  {
    std::lock_guard<std::mutex> lock(audioMutex);
    track->buffer.insert(track->buffer.end(),
                         outPtr,
                         outPtr + convertedSamples * audioChannels);
  }
  audioCondition.notify_one();

  return true;
}

void VideoPlayer::AudioThreadFunction()
{
  if (!audioClient || !renderClient)
    return;

  HRESULT hr; // Declare hr here

  while (audioThreadRunning)
  {
    std::unique_lock<std::mutex> lock(audioMutex);
    audioCondition.wait(lock, [this] { return HasBufferedAudio() || !audioThreadRunning; });

    if (!audioThreadRunning)
      break;

    if (!HasBufferedAudio())
      continue;

    // Get available buffer space
    UINT32 numFramesPadding;
    hr = audioClient->GetCurrentPadding(&numFramesPadding);
    if (FAILED(hr))
      continue;

    UINT32 numFramesAvailable = bufferFrameCount - numFramesPadding;
    if (numFramesAvailable == 0)
    {
      lock.unlock();
      Sleep(1);
      continue;
    }

    // Get render buffer
    BYTE *pData;
    hr = renderClient->GetBuffer(numFramesAvailable, &pData);
    if (FAILED(hr))
      continue;

    // Mix audio from all tracks
    MixAudioTracks(pData, numFramesAvailable);

    hr = renderClient->ReleaseBuffer(numFramesAvailable, 0);
    if (FAILED(hr))
      continue;

    lock.unlock();
  }

  audioClient->Stop();
}

void VideoPlayer::MixAudioTracks(uint8_t *outputBuffer, int frameCount)
{
  memset(outputBuffer, 0, frameCount * audioChannels * sizeof(int16_t));
  int16_t *out = reinterpret_cast<int16_t*>(outputBuffer);
  int totalSamples = frameCount * audioChannels;

  for (int frame = 0; frame < frameCount; ++frame)
  {
    std::vector<int32_t> mix(audioChannels, 0);
    for (auto& track : audioTracks)
    {
      if (track->isMuted)
        continue;
      if (track->buffer.size() >= static_cast<size_t>(audioChannels))
      {
        for (int ch = 0; ch < audioChannels; ++ch)
        {
          int16_t val = track->buffer.front();
          track->buffer.pop_front();
          mix[ch] += static_cast<int32_t>(val * track->volume);
        }
      }
    }
    for (int ch = 0; ch < audioChannels; ++ch)
    {
      int32_t v = mix[ch];
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      out[frame * audioChannels + ch] = static_cast<int16_t>(v);
    }
  }
}

bool VideoPlayer::HasBufferedAudio() const
{
  for (const auto& track : audioTracks)
  {
    if (!track->buffer.empty())
      return true;
  }
  return false;
}

void VideoPlayer::PlaybackThreadFunction()
{
  auto startTime = std::chrono::high_resolution_clock::now();
  double startPts = currentPts;
  while (playbackThreadRunning)
  {
    if (!DecodeNextFrame(false))
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
                           int maxBitrate, HWND progressBar)
{
    if (!isLoaded) {
        DebugLog("CutVideo called but no video loaded", true);
        return false;
    }

    {
        std::ostringstream oss;
        oss << "CutVideo start start=" << startTime << " end=" << endTime
            << " mergeAudio=" << mergeAudio
            << " convertH264=" << convertH264
            << " maxBitrate=" << maxBitrate;
        DebugLog(oss.str());
    }

    int bufSize = WideCharToMultiByte(CP_UTF8, 0, outputFilename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8Output(bufSize, 0);
    WideCharToMultiByte(CP_UTF8, 0, outputFilename.c_str(), -1, &utf8Output[0], bufSize, nullptr, nullptr);
    utf8Output.resize(bufSize - 1);

    bufSize = WideCharToMultiByte(CP_UTF8, 0, loadedFilename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8Input(bufSize, 0);
    WideCharToMultiByte(CP_UTF8, 0, loadedFilename.c_str(), -1, &utf8Input[0], bufSize, nullptr, nullptr);
    utf8Input.resize(bufSize - 1);

    std::vector<int> activeTracks;
    for (const auto& track : audioTracks) {
        if (!track->isMuted)
            activeTracks.push_back(track->streamIndex);
    }
    {
        std::ostringstream oss;
        oss << "Active tracks:";
        for (int idx : activeTracks) oss << ' ' << idx;
        DebugLog(oss.str());
    }

    // When re-encoding or merging audio we need to set up decoder/encoder
    // contexts. The previous implementation only supported stream copying.
    // Build encoder state on demand.
    bool success = true;
    AVCodecContext* vEncCtx = nullptr;
    AVCodecContext* vDecCtx = nullptr;
    SwsContext*     swsCtx  = nullptr;
    AVFrame*        encFrame = nullptr;
    AVFrame*        decFrame = nullptr;

    struct MergeTrack {
        int index;
        AVCodecContext* decCtx;
        SwrContext* swrCtx;
        AVFrame* frame;
        std::deque<int16_t> buffer;
    };
    std::vector<MergeTrack> mergeTracks;
    AVCodecContext* aEncCtx = nullptr;
    int encFrameSamples = 0;
    std::vector<int16_t> mixBuffer;
    SwrContext* mixSwr = nullptr;
    bool headerWritten = false;

    bool needReencode = convertH264 || mergeAudio;

    AVFormatContext* inputCtx = nullptr;
    if (avformat_open_input(&inputCtx, utf8Input.c_str(), nullptr, nullptr) < 0) {
        DebugLog("Failed to open input file", true);
        return false;
    }
    DebugLog("Input opened");
    if (avformat_find_stream_info(inputCtx, nullptr) < 0) {
        DebugLog("Failed to read stream info", true);
        avformat_close_input(&inputCtx);
        return false;
    }
    {
        std::ostringstream oss;
        oss << "Input streams=" << inputCtx->nb_streams;
        DebugLog(oss.str());
    }

    AVFormatContext* outputCtx = nullptr;
    if (avformat_alloc_output_context2(&outputCtx, nullptr, nullptr, utf8Output.c_str()) < 0) {
        DebugLog("Failed to allocate output context", true);
        avformat_close_input(&inputCtx);
        return false;
    }
    DebugLog("Output context allocated");

    std::vector<int> streamMapping(inputCtx->nb_streams, -1);
    int mergedAudioIndex = -1;
    for (unsigned i = 0; i < inputCtx->nb_streams; ++i) {
        AVStream* inStream = inputCtx->streams[i];
        bool useStream = (inStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && i == (unsigned)videoStreamIndex);
        if (!useStream && inStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            useStream = std::find(activeTracks.begin(), activeTracks.end(), (int)i) != activeTracks.end();
        }
        if (!useStream)
            continue;

        AVStream* outStream = nullptr;
        if (needReencode && inStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && i == (unsigned)videoStreamIndex && convertH264) {
            const AVCodec* vEnc = avcodec_find_encoder(AV_CODEC_ID_H264);
            if (!vEnc) {
                DebugLog("H.264 encoder not found", true);
                avformat_free_context(outputCtx);
                avformat_close_input(&inputCtx);
                return false;
            }
            outStream = avformat_new_stream(outputCtx, vEnc);
            vEncCtx = avcodec_alloc_context3(vEnc);
            vEncCtx->codec_id = AV_CODEC_ID_H264;
            vEncCtx->width = inStream->codecpar->width;
            vEncCtx->height = inStream->codecpar->height;
            vEncCtx->time_base = inStream->time_base;
            vEncCtx->pix_fmt = AV_PIX_FMT_YUV420P;
            vEncCtx->max_b_frames = 2;
            vEncCtx->gop_size = 12;
            if (maxBitrate > 0)
                vEncCtx->bit_rate = maxBitrate * 1000;
            if (outputCtx->oformat->flags & AVFMT_GLOBALHEADER)
                vEncCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            AVDictionary* encOpts = nullptr;
            av_dict_set(&encOpts, "preset", "fast", 0);
            if (avcodec_open2(vEncCtx, vEnc, &encOpts) < 0) {
                DebugLog("Failed to open H.264 encoder", true);
                avcodec_free_context(&vEncCtx);
                avformat_free_context(outputCtx);
                avformat_close_input(&inputCtx);
                av_dict_free(&encOpts);
                return false;
            }
            av_dict_free(&encOpts);
            if (avcodec_parameters_from_context(outStream->codecpar, vEncCtx) < 0) {
                DebugLog("Failed to copy encoder parameters", true);
                success = false;
                goto cleanup;
            }
            outStream->time_base = vEncCtx->time_base;
            vDecCtx = avcodec_alloc_context3(avcodec_find_decoder(inStream->codecpar->codec_id));
            if (!vDecCtx ||
                avcodec_parameters_to_context(vDecCtx, inStream->codecpar) < 0) {
                DebugLog("Failed to create video decoder context", true);
                avcodec_free_context(&vEncCtx);
                if (vDecCtx) avcodec_free_context(&vDecCtx);
                avformat_free_context(outputCtx);
                avformat_close_input(&inputCtx);
                return false;
            }
            if (avcodec_open2(vDecCtx, avcodec_find_decoder(inStream->codecpar->codec_id), nullptr) < 0) {
                DebugLog("Failed to open video decoder", true);
                avcodec_free_context(&vEncCtx);
                avcodec_free_context(&vDecCtx);
                avformat_free_context(outputCtx);
                avformat_close_input(&inputCtx);
                return false;
            }
            DebugLog("Video decoder/encoder initialized");
            swsCtx = nullptr; // initialized after first decoded frame
            encFrame = av_frame_alloc();
            decFrame = av_frame_alloc();
            if (!encFrame || !decFrame) {
                DebugLog("Failed to allocate frames", true);
                success = false;
                goto cleanup;
            }
            encFrame->format = vEncCtx->pix_fmt;
            encFrame->width = vEncCtx->width;
            encFrame->height = vEncCtx->height;
            if (av_frame_get_buffer(encFrame, 32) < 0) {
                DebugLog("Failed to allocate buffer for encoder frame", true);
                success = false;
                goto cleanup;
            }
        } else if (needReencode && inStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && mergeAudio) {
            // We'll create a single output audio stream later
            MergeTrack mt{};
            mt.index = i;
            const AVCodec* dec = avcodec_find_decoder(inStream->codecpar->codec_id);
            mt.decCtx = avcodec_alloc_context3(dec);
            avcodec_parameters_to_context(mt.decCtx, inStream->codecpar);
            avcodec_open2(mt.decCtx, dec, nullptr);
            mt.swrCtx = swr_alloc();
            av_opt_set_int(mt.swrCtx, "in_sample_rate", mt.decCtx->sample_rate, 0);
            av_opt_set_int(mt.swrCtx, "out_sample_rate", 44100, 0);
            av_opt_set_sample_fmt(mt.swrCtx, "in_sample_fmt", mt.decCtx->sample_fmt, 0);
            av_opt_set_sample_fmt(mt.swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
            av_channel_layout_default(&mt.decCtx->ch_layout,
                                      mt.decCtx->ch_layout.nb_channels ?
                                          mt.decCtx->ch_layout.nb_channels : 2);
            AVChannelLayout out_ch{};
            av_channel_layout_default(&out_ch, 2);
            av_opt_set_chlayout(mt.swrCtx, "in_chlayout", &mt.decCtx->ch_layout, 0);
            av_opt_set_chlayout(mt.swrCtx, "out_chlayout", &out_ch, 0);
            swr_init(mt.swrCtx);
            mt.frame = av_frame_alloc();
            mergeTracks.push_back(mt);
            continue; // output stream created later
        } else {
            outStream = avformat_new_stream(outputCtx, nullptr);
            if (avcodec_parameters_copy(outStream->codecpar, inStream->codecpar) < 0) {
                DebugLog("Failed to copy codec parameters", true);
                avformat_free_context(outputCtx);
                avformat_close_input(&inputCtx);
                return false;
            }
            outStream->codecpar->codec_tag = 0;
            outStream->time_base = inStream->time_base;
        }
        streamMapping[i] = outStream ? outStream->index : -1;
    }

    if (mergeAudio && !mergeTracks.empty()) {
        const AVCodec* aEnc = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!aEnc) {
            DebugLog("AAC encoder not found", true);
            avformat_free_context(outputCtx);
            avformat_close_input(&inputCtx);
            return false;
        }
        AVStream* aOut = avformat_new_stream(outputCtx, aEnc);
        aEncCtx = avcodec_alloc_context3(aEnc);
        if (!aEncCtx) {
            DebugLog("Failed to allocate AAC encoder context", true);
            avformat_free_context(outputCtx);
            avformat_close_input(&inputCtx);
            return false;
        }
        aEncCtx->sample_rate = 44100;
        av_channel_layout_default(&aEncCtx->ch_layout, 2);
        aEncCtx->sample_fmt = aEnc->sample_fmts ? aEnc->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        aEncCtx->time_base = {1, aEncCtx->sample_rate};
        aEncCtx->bit_rate = 128000; // match ffmpeg default
        if (avcodec_open2(aEncCtx, aEnc, nullptr) < 0) {
            DebugLog("Failed to open AAC encoder", true);
            avcodec_free_context(&aEncCtx);
            avformat_free_context(outputCtx);
            avformat_close_input(&inputCtx);
            return false;
        }
        DebugLog("AAC encoder initialized");
        if (avcodec_parameters_from_context(aOut->codecpar, aEncCtx) < 0) {
            DebugLog("Failed to copy AAC encoder parameters", true);
            success = false;
            goto cleanup;
        }
        aOut->time_base = aEncCtx->time_base;
        encFrameSamples = aEncCtx->frame_size > 0 ? aEncCtx->frame_size : 1024;
        if (aEncCtx->ch_layout.nb_channels <= 0) {
            DebugLog("Invalid channel count in AAC encoder context", true);
            success = false;
            goto cleanup;
        }
        mixBuffer.resize(encFrameSamples * aEncCtx->ch_layout.nb_channels);
        mixSwr = swr_alloc();
        AVChannelLayout stereo;
        av_channel_layout_default(&stereo, 2);
        av_opt_set_int   (mixSwr, "in_sample_rate", 44100, 0);
        av_opt_set_sample_fmt(mixSwr, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
        av_opt_set_chlayout  (mixSwr, "in_chlayout", &stereo, 0);
        av_opt_set_int   (mixSwr, "out_sample_rate", aEncCtx->sample_rate, 0);
        av_opt_set_sample_fmt(mixSwr, "out_sample_fmt", aEncCtx->sample_fmt, 0);
        av_opt_set_chlayout  (mixSwr, "out_chlayout", &aEncCtx->ch_layout, 0);
        if (swr_init(mixSwr) < 0) {
            DebugLog("Failed to init mix resampler", true);
            success = false;
            goto cleanup;
        }
        mergedAudioIndex = aOut->index;
    }

    if (!(outputCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputCtx->pb, utf8Output.c_str(), AVIO_FLAG_WRITE) < 0) {
            DebugLog("Could not open output file", true);
            avformat_free_context(outputCtx);
            avformat_close_input(&inputCtx);
            return false;
        }
    }

    if (avformat_write_header(outputCtx, nullptr) < 0) {
        DebugLog("Failed to write header", true);
        if (!(outputCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outputCtx->pb);
        avformat_free_context(outputCtx);
        avformat_close_input(&inputCtx);
        return false;
    }
    DebugLog("Header written");
    headerWritten = true;
    DebugLog("Beginning packet processing");

    int64_t startPts = (int64_t)(startTime * AV_TIME_BASE);
    int64_t endPts = (int64_t)(endTime * AV_TIME_BASE);
    if (av_seek_frame(inputCtx, -1, startPts, AVSEEK_FLAG_BACKWARD) < 0) {
        DebugLog("Seek failed", true);
    }

    AVPacket pkt, outPkt;
    av_init_packet(&pkt);
    av_init_packet(&outPkt); // ensure fields are zeroed before use
    int64_t audioPts = 0;
    while (av_read_frame(inputCtx, &pkt) >= 0) {
        bool handled = false;
        AVStream* inStream = inputCtx->streams[pkt.stream_index];
        int64_t pktPtsUs = av_rescale_q(pkt.pts, inStream->time_base, AV_TIME_BASE_Q);
        if (pktPtsUs < startPts) { av_packet_unref(&pkt); continue; }
        if (pktPtsUs > endPts) { av_packet_unref(&pkt); break; }

        if (convertH264 && pkt.stream_index == videoStreamIndex) {
            avcodec_send_packet(vDecCtx, &pkt);
            while (avcodec_receive_frame(vDecCtx, decFrame) == 0) {
                if (!swsCtx) {
                    swsCtx = sws_getContext(vDecCtx->width, vDecCtx->height,
                                            (AVPixelFormat)decFrame->format,
                                            vEncCtx->width, vEncCtx->height,
                                            vEncCtx->pix_fmt, SWS_BILINEAR,
                                            nullptr, nullptr, nullptr);
                    if (!swsCtx) {
                        DebugLog("Failed to create scaling context", true);
                        av_packet_unref(&pkt);
                        success = false;
                        goto cleanup;
                    }
                }
                sws_scale(swsCtx, decFrame->data, decFrame->linesize, 0, vDecCtx->height, encFrame->data, encFrame->linesize);
                encFrame->pts = av_rescale_q(decFrame->pts - av_rescale_q(startPts, AV_TIME_BASE_Q, inStream->time_base), inStream->time_base, vEncCtx->time_base);
                avcodec_send_frame(vEncCtx, encFrame);
                while (avcodec_receive_packet(vEncCtx, &outPkt) == 0) {
                    av_packet_rescale_ts(&outPkt, vEncCtx->time_base, outputCtx->streams[streamMapping[pkt.stream_index]]->time_base);
                    outPkt.stream_index = streamMapping[pkt.stream_index];
                    av_interleaved_write_frame(outputCtx, &outPkt);
                    av_packet_unref(&outPkt);
                }
                av_frame_unref(decFrame);
            }
            handled = true;
        } else if (mergeAudio) {
            for (auto &mt : mergeTracks) {
                if (mt.index == pkt.stream_index) {
                    avcodec_send_packet(mt.decCtx, &pkt);
                    while (avcodec_receive_frame(mt.decCtx, mt.frame) == 0) {
                        int outSamples = swr_get_out_samples(mt.swrCtx, mt.frame->nb_samples);
                        std::vector<int16_t> tmp(outSamples * 2);
                        uint8_t* outArr[1] = { reinterpret_cast<uint8_t*>(tmp.data()) };
                        int conv = swr_convert(mt.swrCtx, outArr, outSamples,
                                              (const uint8_t**)mt.frame->data,
                                              mt.frame->nb_samples);
                        mt.buffer.insert(mt.buffer.end(), tmp.begin(),
                                          tmp.begin() + conv * 2);
                    }
                    handled = true;
                    break;
                }
            }
        }

        if (!handled) {
            if (pkt.stream_index >= (int)streamMapping.size() || streamMapping[pkt.stream_index] < 0) {
                av_packet_unref(&pkt);
                continue;
            }
            AVStream* outStream = outputCtx->streams[streamMapping[pkt.stream_index]];
            pkt.pts = av_rescale_q(pkt.pts - av_rescale_q(startPts, AV_TIME_BASE_Q, inStream->time_base), inStream->time_base, outStream->time_base);
            pkt.dts = av_rescale_q(pkt.dts - av_rescale_q(startPts, AV_TIME_BASE_Q, inStream->time_base), inStream->time_base, outStream->time_base);
            if (pkt.duration > 0)
                pkt.duration = av_rescale_q(pkt.duration, inStream->time_base, outStream->time_base);
            pkt.pos = -1;
            pkt.stream_index = outStream->index;
            av_interleaved_write_frame(outputCtx, &pkt);
        }

        av_packet_unref(&pkt);

        // check if we can encode audio frame
        if (mergeAudio && !mergeTracks.empty()) {
            bool ready = true;
            for (auto &mt : mergeTracks)
                if ((int)mt.buffer.size() < encFrameSamples * 2) { ready = false; break; }
            if (ready) {
                for (int i = 0; i < encFrameSamples * 2; ++i) {
                    int sum = 0;
                    for (auto &mt : mergeTracks) {
                        sum += mt.buffer.front();
                        mt.buffer.pop_front();
                    }
                    int v = sum / (int)mergeTracks.size();
                    if (v > 32767) v = 32767;
                    if (v < -32768) v = -32768;
                    mixBuffer[i] = (int16_t)v;
                }
                AVFrame* af = av_frame_alloc();
                af->nb_samples = encFrameSamples;
                av_channel_layout_copy(&af->ch_layout, &aEncCtx->ch_layout);
                af->format = aEncCtx->sample_fmt;
                af->sample_rate = aEncCtx->sample_rate;
                if (av_frame_get_buffer(af, 0) < 0) {
                    DebugLog("Failed to allocate audio frame buffer", true);
                    av_frame_free(&af);
                    success = false;
                    goto cleanup;
                }
                const uint8_t* inBuf[1] = { (const uint8_t*)mixBuffer.data() };
                if (swr_convert(mixSwr, af->data, encFrameSamples, inBuf, encFrameSamples) < 0) {
                    DebugLog("Failed to convert mixed samples", true);
                    av_frame_free(&af);
                    success = false;
                    goto cleanup;
                }
                af->pts = audioPts;
                audioPts += encFrameSamples;
                avcodec_send_frame(aEncCtx, af);
                while (avcodec_receive_packet(aEncCtx, &outPkt) == 0) {
                    av_packet_rescale_ts(&outPkt, aEncCtx->time_base, outputCtx->streams[mergedAudioIndex]->time_base);
                    outPkt.stream_index = mergedAudioIndex;
                    av_interleaved_write_frame(outputCtx, &outPkt);
                    av_packet_unref(&outPkt);
                }
                av_frame_free(&af);
            }
        }

        double progress = (pktPtsUs - startPts) / double(endPts - startPts);
        SendMessage(progressBar, PBM_SETPOS, (int)(progress * 100.0), 0);
    }

    // Flush encoders
    DebugLog("Flushing encoders");
    if (convertH264 && vEncCtx) {
        avcodec_send_frame(vEncCtx, nullptr);
        while (avcodec_receive_packet(vEncCtx, &outPkt) == 0) {
            av_packet_rescale_ts(&outPkt, vEncCtx->time_base, outputCtx->streams[streamMapping[videoStreamIndex]]->time_base);
            outPkt.stream_index = streamMapping[videoStreamIndex];
            av_interleaved_write_frame(outputCtx, &outPkt);
            av_packet_unref(&outPkt);
        }
    }
    if (mergeAudio && aEncCtx) {
        // flush remaining samples
        while (true) {
            bool ready = true;
            for (auto &mt : mergeTracks)
                if ((int)mt.buffer.size() < encFrameSamples * 2) { ready = false; break; }
            if (!ready) break;
            for (int i = 0; i < encFrameSamples * 2; ++i) {
                int sum = 0;
                for (auto &mt : mergeTracks) { sum += mt.buffer.front(); mt.buffer.pop_front(); }
                int v = sum / (int)mergeTracks.size();
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                mixBuffer[i] = (int16_t)v;
            }
            AVFrame* af = av_frame_alloc();
            af->nb_samples = encFrameSamples;
            av_channel_layout_copy(&af->ch_layout, &aEncCtx->ch_layout);
            af->format = aEncCtx->sample_fmt;
            af->sample_rate = aEncCtx->sample_rate;
            if (av_frame_get_buffer(af, 0) < 0) {
                DebugLog("Failed to allocate audio frame buffer", true);
                av_frame_free(&af);
                success = false;
                goto cleanup;
            }
            const uint8_t* inBuf[1] = { (const uint8_t*)mixBuffer.data() };
            if (swr_convert(mixSwr, af->data, encFrameSamples, inBuf, encFrameSamples) < 0) {
                DebugLog("Failed to convert mixed samples", true);
                av_frame_free(&af);
                success = false;
                goto cleanup;
            }
            af->pts = audioPts;
            audioPts += encFrameSamples;
            avcodec_send_frame(aEncCtx, af);
            while (avcodec_receive_packet(aEncCtx, &outPkt) == 0) {
                av_packet_rescale_ts(&outPkt, aEncCtx->time_base, outputCtx->streams[mergedAudioIndex]->time_base);
                outPkt.stream_index = mergedAudioIndex;
                av_interleaved_write_frame(outputCtx, &outPkt);
                av_packet_unref(&outPkt);
            }
            av_frame_free(&af);
        }
        avcodec_send_frame(aEncCtx, nullptr);
        while (avcodec_receive_packet(aEncCtx, &outPkt) == 0) {
            av_packet_rescale_ts(&outPkt, aEncCtx->time_base, outputCtx->streams[mergedAudioIndex]->time_base);
            outPkt.stream_index = mergedAudioIndex;
            av_interleaved_write_frame(outputCtx, &outPkt);
            av_packet_unref(&outPkt);
        }
    }

cleanup:
    DebugLog("Entering cleanup");
    if (headerWritten)
        av_write_trailer(outputCtx);
    if (!(outputCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outputCtx->pb);
    if (vEncCtx) avcodec_free_context(&vEncCtx);
    if (vDecCtx) avcodec_free_context(&vDecCtx);
    if (swsCtx) sws_freeContext(swsCtx);
    if (encFrame) av_frame_free(&encFrame);
    if (decFrame) av_frame_free(&decFrame);
    if (aEncCtx) avcodec_free_context(&aEncCtx);
    if (mixSwr) swr_free(&mixSwr);
    for (auto &mt : mergeTracks) {
        if (mt.swrCtx) swr_free(&mt.swrCtx);
        if (mt.decCtx) avcodec_free_context(&mt.decCtx);
        if (mt.frame) av_frame_free(&mt.frame);
    }
    avformat_free_context(outputCtx);
    avformat_close_input(&inputCtx);

    SendMessage(progressBar, PBM_SETPOS, 100, 0);

    DebugLog("CutVideo finished");

    return success;
}

bool VideoPlayer::InitializeD2D()
{
  return SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory));
}

bool VideoPlayer::CreateRenderTarget()
{
  if (!d2dFactory || !videoWindow)
    return false;
  RECT rc;
  GetClientRect(videoWindow, &rc);
  D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
  HRESULT hr = d2dFactory->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(),
      D2D1::HwndRenderTargetProperties(videoWindow, size),
      &d2dRenderTarget);
  return SUCCEEDED(hr);
}

void VideoPlayer::CleanupD2D()
{
  if (d2dBitmap)
  {
    d2dBitmap->Release();
    d2dBitmap = nullptr;
  }
  if (d2dRenderTarget)
  {
    d2dRenderTarget->Release();
    d2dRenderTarget = nullptr;
  }
  if (d2dFactory)
  {
    d2dFactory->Release();
    d2dFactory = nullptr;
  }
}

LRESULT CALLBACK VideoPlayer::VideoWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  VideoPlayer* player = reinterpret_cast<VideoPlayer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  if (player)
  {
    if (msg == WM_PAINT)
    {
      player->OnVideoWindowPaint();
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

void VideoPlayer::OnVideoWindowPaint()
{
  PAINTSTRUCT ps;
  BeginPaint(videoWindow, &ps);
  if (isLoaded)
    UpdateDisplay();
  else
    FillRect(ps.hdc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
  EndPaint(videoWindow, &ps);
}
