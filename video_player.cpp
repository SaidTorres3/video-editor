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
      frame(nullptr), frameRGB(nullptr), packet(nullptr), swsContext(nullptr),
      buffer(nullptr), hwDeviceCtx(nullptr), hwPixelFormat(AV_PIX_FMT_NONE),
      videoStreamIndex(-1), frameWidth(0), frameHeight(0),
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
  DebugLog("Destroying VideoPlayer");
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
  if (hwDeviceCtx)
    av_buffer_unref(&hwDeviceCtx);
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

  {
    int buf = WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string tmp(buf, 0);
    WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, &tmp[0], buf, nullptr, nullptr);
    tmp.resize(buf - 1);
    DebugLog(std::string("Loading video: ") + tmp);
  }

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
  DebugLog("Video loaded successfully");
  return true;
}

bool VideoPlayer::InitializeDecoder()
{
  AVStream *vs = formatContext->streams[videoStreamIndex];
  AVCodecParameters *cp = vs->codecpar;
  const AVCodec *codec = avcodec_find_decoder(cp->codec_id);
  if (!codec)
    return false;

  {
    std::ostringstream oss;
    oss << "Initializing decoder for codec " << avcodec_get_name(cp->codec_id);
    DebugLog(oss.str());
  }

  bool useHW = false;
  AVPixelFormat hwfmt = AV_PIX_FMT_NONE;
  hwPixelFormat = AV_PIX_FMT_NONE;
  if (cp->codec_id == AV_CODEC_ID_H264)
  {
    DebugLog("Checking for D3D11 hardware acceleration");
    for (int i = 0;; i++)
    {
      const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
      if (!config)
        break;
      if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
          config->device_type == AV_HWDEVICE_TYPE_D3D11VA)
      {
        hwfmt = config->pix_fmt;
        useHW = true;
        DebugLog("D3D11 hardware config found");
        break;
      }
    }
  }

  codecContext = avcodec_alloc_context3(codec);
  if (!codecContext)
    return false;
  if (avcodec_parameters_to_context(codecContext, cp) < 0)
  {
    avcodec_free_context(&codecContext);
    return false;
  }
  if (useHW)
  {
    codecContext->opaque = this;
    codecContext->get_format = GetHWFormat;
    if (av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0) >= 0)
    {
      codecContext->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
      hwPixelFormat = hwfmt;
      DebugLog("Created D3D11 device for hardware decode");
    }
    else
    {
      DebugLog("Failed to create D3D11 device, falling back to software");
      useHW = false;
      hwPixelFormat = AV_PIX_FMT_NONE;
      codecContext->get_format = nullptr;
      codecContext->opaque = nullptr;
    }
  }
  if (avcodec_open2(codecContext, codec, nullptr) < 0)
  {
    avcodec_free_context(&codecContext);
    if (hwDeviceCtx)
      av_buffer_unref(&hwDeviceCtx);
    return false;
  }

  {
    DebugLog(std::string("Decoder opened, using ") + (useHW ? "D3D11" : "software") + " mode");
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

  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, frameWidth, frameHeight, 32);
  buffer = (uint8_t *)av_malloc(numBytes);
  av_image_fill_arrays(frameRGB->data, frameRGB->linesize, buffer,
                       AV_PIX_FMT_BGRA, frameWidth, frameHeight, 32);

  AVPixelFormat srcFmt = useHW ? codecContext->sw_pix_fmt : codecContext->pix_fmt;
  swsContext = sws_getContext(
      frameWidth, frameHeight, srcFmt,
      frameWidth, frameHeight, AV_PIX_FMT_BGRA,
      SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
  if (!swsContext)
  {
    DebugLog("sws_getContext failed");
    CleanupDecoder();
    return false;
  }

  return true;
}

void VideoPlayer::CleanupDecoder()
{
  DebugLog("Cleaning up decoder");
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
  if (hwDeviceCtx)
    av_buffer_unref(&hwDeviceCtx), hwDeviceCtx = nullptr;
  hwPixelFormat = AV_PIX_FMT_NONE;
}

void VideoPlayer::UnloadVideo()
{
  DebugLog("Unloading video");
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
  DebugLog("Playback started");
  
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
    DebugLog("Playback paused");
    
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
  DebugLog("Playback stopped");
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

bool VideoPlayer::DecodeNextFrame()
{
  if (!isLoaded)
    return false;

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
        ret = avcodec_receive_frame(codecContext, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
          break;
        if (ret < 0)
        {
          DebugLog("avcodec_receive_frame failed");
          return false;
        }
        AVFrame *src = frame;
        AVFrame *tmp = nullptr;
        if (frame->format == hwPixelFormat)
        {
          DebugLog("Transferring hardware frame to system memory");
          tmp = av_frame_alloc();
          if (!tmp)
          {
            DebugLog("av_frame_alloc failed");
            return false;
          }
          tmp->format = codecContext->sw_pix_fmt;
          tmp->width = frame->width;
          tmp->height = frame->height;
          if (av_frame_get_buffer(tmp, 0) < 0 ||
              av_hwframe_transfer_data(tmp, frame, 0) < 0)
          {
            DebugLog("av_hwframe_transfer_data failed");
            av_frame_free(&tmp);
            return false;
          }
          src = tmp;
        }

        sws_scale(
            swsContext,
            (uint8_t const *const *)src->data, src->linesize,
            0, frameHeight,
            frameRGB->data, frameRGB->linesize);
        if (tmp)
          av_frame_free(&tmp);
        av_frame_unref(frame);
        AVStream *vs = formatContext->streams[videoStreamIndex];
        double pts = 0.0;
        if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
          pts = frame->best_effort_timestamp * av_q2d(vs->time_base);
        else if (frame->pts != AV_NOPTS_VALUE)
          pts = frame->pts * av_q2d(vs->time_base);
        else
          pts = currentPts + (frameRate > 0 ? 1.0 / frameRate : 0.0);
        currentPts = pts - startTimeOffset;
        if (currentPts < 0.0)
          currentPts = 0.0;
        currentFrame++;
        UpdateDisplay();
        DebugLog("Frame decoded");
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

  // Decode the frame at the requested timestamp to update the preview
  DecodeNextFrame();
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
    if (!DecodeNextFrame())
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

    std::ostringstream cmd;
    cmd << "ffmpeg -y -ss " << startTime << " -to " << endTime << " -i \"" << utf8Input << "\" ";

    if (mergeAudio && activeTracks.size() > 1) {
        cmd << "-filter_complex \"";
        for (size_t i = 0; i < activeTracks.size(); ++i) {
            cmd << "[0:" << activeTracks[i] << "]";
        }
        cmd << "amix=inputs=" << activeTracks.size() << "[aout]\" -map 0:v -map [aout] ";
        cmd << "-c:a aac -b:a 192k ";
    } else {
        cmd << "-map 0:v ";
        for (size_t i = 0; i < activeTracks.size(); ++i) {
            cmd << "-map 0:" << activeTracks[i] << " ";
        }
        cmd << "-c:a copy ";
    }

    if (convertH264) {
        cmd << "-c:v libx264 ";
        if (maxBitrate > 0)
            cmd << "-maxrate " << maxBitrate << "k ";
        cmd << "-pix_fmt yuv420p ";
    } else {
        cmd << "-c:v copy ";
    }

    cmd << "-progress pipe:1 -nostats \"" << utf8Output << "\"";

    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE readPipe = NULL, writePipe = NULL;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        DebugLog("CreatePipe failed", true);
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi{};
    std::string cmdStr = cmd.str();

    std::vector<char> cmdBuf(cmdStr.begin(), cmdStr.end());
    cmdBuf.push_back('\0');
    DebugLog(std::string("Running command: ") + cmdStr, true);
    BOOL ok = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        DebugLog("CreateProcess failed", true);
        CloseHandle(readPipe);
        return false;
    }
    SendMessage(progressBar, PBM_SETPOS, 0, 0);

    char buffer[256];
    std::string line;
    DWORD bytesRead = 0;
    double totalMs = (endTime - startTime) * 1000.0;
    while (ReadFile(readPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
        if (bytesRead == 0) break;
        buffer[bytesRead] = 0;
        line.append(buffer);
        size_t pos;
        while ((pos = line.find('\n')) != std::string::npos) {
            std::string l = line.substr(0, pos);
            line.erase(0, pos + 1);
            DebugLog(l);
            if (l.rfind("out_time_ms=", 0) == 0) {
                long long ms = _atoi64(l.substr(12).c_str()) / 1000;
                int percent = static_cast<int>((ms / totalMs) * 100.0);
                SendMessage(progressBar, PBM_SETPOS, percent, 0);
            }
        }
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    {
        std::ostringstream oss;
        oss << "ffmpeg exited with code " << exitCode;
        DebugLog(oss.str(), exitCode != 0);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(readPipe);

    SendMessage(progressBar, PBM_SETPOS, 100, 0);

    return exitCode == 0;
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

enum AVPixelFormat VideoPlayer::GetHWFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
  VideoPlayer *player = reinterpret_cast<VideoPlayer *>(ctx->opaque);
  for (const enum AVPixelFormat *p = pix_fmts; *p != -1; p++)
  {
    if (*p == player->hwPixelFormat)
      return *p;
  }
  return pix_fmts[0];
}
