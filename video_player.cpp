// video_player.cpp
#include "video_player.h"
#include <iostream>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dxgiformat.h>
#pragma comment(lib, "d2d1.lib")
#include <uxtheme.h>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <cstdio>

// Link with Windows Audio libraries
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

VideoPlayer::VideoPlayer(HWND parent)
    : parentWindow(parent), formatContext(nullptr), codecContext(nullptr),
      frame(nullptr), frameRGB(nullptr), packet(nullptr), swsContext(nullptr),
      buffer(nullptr), videoStreamIndex(-1), frameWidth(0), frameHeight(0),
      isLoaded(false), isPlaying(false), frameRate(0), currentFrame(0),
      totalFrames(0), currentPts(0.0), duration(0.0), videoWindow(nullptr),
      d2dFactory(nullptr), d2dRenderTarget(nullptr), d2dBitmap(nullptr), playbackTimer(0),
      deviceEnumerator(nullptr), audioDevice(nullptr), audioClient(nullptr),
      renderClient(nullptr), audioFormat(nullptr), bufferFrameCount(0),
      audioInitialized(false), audioThreadRunning(false),
      playbackThreadRunning(false),
      audioSampleRate(44100), audioChannels(2), audioSampleFormat(AV_SAMPLE_FMT_S16)
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
  {
    SetWindowTheme(videoWindow, L"DarkMode_Explorer", nullptr);
    CreateRenderTarget();
  }
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

  // Initialize audio tracks
  if (!InitializeAudioTracks())
  {
    std::cout << "Warning: Failed to initialize audio tracks" << std::endl;
  }

  isLoaded = true;
  loadedFilename = filename;
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

  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, frameWidth, frameHeight, 32);
  buffer = (uint8_t *)av_malloc(numBytes);
  av_image_fill_arrays(frameRGB->data, frameRGB->linesize, buffer,
                       AV_PIX_FMT_BGRA, frameWidth, frameHeight, 32);

  swsContext = sws_getContext(
      frameWidth, frameHeight, codecContext->pix_fmt,
      frameWidth, frameHeight, AV_PIX_FMT_BGRA,
      SWS_BILINEAR, nullptr, nullptr, nullptr);
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
  if (frame)
    av_frame_free(&frame), frame = nullptr;
  if (codecContext)
    avcodec_free_context(&codecContext), codecContext = nullptr;
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
  loadedFilename.clear();
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
          return false;

        sws_scale(
            swsContext,
            (uint8_t const *const *)frame->data, frame->linesize,
            0, frameHeight,
            frameRGB->data, frameRGB->linesize);
        AVStream *vs = formatContext->streams[videoStreamIndex];
        if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
          currentPts = frame->best_effort_timestamp * av_q2d(vs->time_base);
        else if (frame->pts != AV_NOPTS_VALUE)
          currentPts = frame->pts * av_q2d(vs->time_base);
        else
          currentPts += (frameRate > 0 ? 1.0 / frameRate : 0.0);
        currentFrame++;
        UpdateDisplay();
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
  AVStream *vs = formatContext->streams[videoStreamIndex];
  AVRational fps = av_d2q(frameRate, 100000);
  int64_t ts = av_rescale_q(frameNumber, av_inv_q(fps), vs->time_base);
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
  currentFrame = frameNumber;
  currentPts = frameNumber / frameRate;
  DecodeNextFrame();
}

void VideoPlayer::SeekToTime(double seconds)
{
  if (!isLoaded)
    return;
  AVStream *vs = formatContext->streams[videoStreamIndex];
  int64_t ts = (int64_t)(seconds / av_q2d(vs->time_base));
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
  currentFrame = (int64_t)(seconds * frameRate);
  currentPts = seconds;
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

  // Resample audio
  int outSamples = swr_get_out_samples(track->swrContext, track->frame->nb_samples);
  std::vector<uint8_t> resampledData(outSamples * audioChannels * sizeof(int16_t));
  uint8_t *outPtr = resampledData.data();
  
  int convertedSamples = swr_convert(track->swrContext, &outPtr, outSamples,
                                    (const uint8_t**)track->frame->data, track->frame->nb_samples);
  if (convertedSamples < 0)
    return false;

  // Store raw samples in track buffer
  resampledData.resize(convertedSamples * audioChannels * sizeof(int16_t));
  {
    std::lock_guard<std::mutex> lock(audioMutex);
    int16_t* samples = reinterpret_cast<int16_t*>(resampledData.data());
    track->buffer.insert(track->buffer.end(),
                         samples,
                         samples + convertedSamples * audioChannels);
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

bool VideoPlayer::CutVideo(const std::wstring &outputFilename, double startTime, double endTime, bool mergeAudio, bool reencodeToH264, int quality)
{
    if (!isLoaded) return false;

    if (reencodeToH264) {
        if (loadedFilename.empty()) return false;
        int inSize = WideCharToMultiByte(CP_UTF8, 0, loadedFilename.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8Input(inSize, 0);
        WideCharToMultiByte(CP_UTF8, 0, loadedFilename.c_str(), -1, &utf8Input[0], inSize, nullptr, nullptr);

        int outSize = WideCharToMultiByte(CP_UTF8, 0, outputFilename.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8Out(outSize, 0);
        WideCharToMultiByte(CP_UTF8, 0, outputFilename.c_str(), -1, &utf8Out[0], outSize, nullptr, nullptr);

        int activeTracks = 0;
        for (const auto &t : audioTracks) {
            if (!t->isMuted) activeTracks++; 
        }
        std::string filterArg;
        if (mergeAudio && activeTracks > 1) {
            filterArg = "-filter_complex amix=inputs=" + std::to_string(activeTracks);
        }
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -y -ss %.3f -to %.3f -i \"%s\" %s -c:v libx264 -crf %d -preset veryfast -c:a aac \"%s\"",
                 startTime, endTime, utf8Input.c_str(), filterArg.c_str(), quality, utf8Out.c_str());
        int result = system(cmd);
        return result == 0;
    }

    // Convert filename to UTF-8
    int bufSize = WideCharToMultiByte(CP_UTF8, 0, outputFilename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8OutputFilename(bufSize, 0);
    WideCharToMultiByte(CP_UTF8, 0, outputFilename.c_str(), -1, &utf8OutputFilename[0], bufSize, nullptr, nullptr);

    AVFormatContext* outFormatContext = nullptr;
    int ret = avformat_alloc_output_context2(&outFormatContext, nullptr, nullptr, utf8OutputFilename.c_str());
    if (ret < 0 || !outFormatContext) {
        std::cerr << "Could not create output context" << std::endl;
        return false;
    }

    std::vector<int> stream_mapping;
    int out_stream_index = 0;
    std::vector<int> unmuted_audio_streams;

    // Map video stream
    stream_mapping.push_back(out_stream_index++);
    AVStream* video_out_stream = avformat_new_stream(outFormatContext, nullptr);
    avcodec_parameters_copy(video_out_stream->codecpar, formatContext->streams[videoStreamIndex]->codecpar);
    video_out_stream->codecpar->codec_tag = 0;
    video_out_stream->time_base = formatContext->streams[videoStreamIndex]->time_base;

    // Identify unmuted audio streams
    for (const auto& track : audioTracks) {
        if (!track->isMuted) {
            unmuted_audio_streams.push_back(track->streamIndex);
        }
    }

    if (mergeAudio && unmuted_audio_streams.size() > 1) {
        // Create a single audio stream for merging
        AVStream* audio_out_stream = avformat_new_stream(outFormatContext, nullptr);
        avcodec_parameters_copy(audio_out_stream->codecpar, formatContext->streams[unmuted_audio_streams[0]]->codecpar);
        audio_out_stream->codecpar->codec_tag = 0;
        audio_out_stream->time_base = formatContext->streams[unmuted_audio_streams[0]]->time_base;
    } else {
        // Map each unmuted audio stream individually
        for (int stream_index : unmuted_audio_streams) {
            stream_mapping.push_back(out_stream_index++);
            AVStream* audio_out_stream = avformat_new_stream(outFormatContext, nullptr);
            avcodec_parameters_copy(audio_out_stream->codecpar, formatContext->streams[stream_index]->codecpar);
            audio_out_stream->codecpar->codec_tag = 0;
            audio_out_stream->time_base = formatContext->streams[stream_index]->time_base;
        }
    }

    // Open output file
    if (!(outFormatContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outFormatContext->pb, utf8OutputFilename.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            // Error handling
            avformat_free_context(outFormatContext);
            return false;
        }
    }

    AVDictionary* opts = nullptr;
    if (mergeAudio && unmuted_audio_streams.size() > 1) {
        std::string filter_spec;
        for (int stream_index : unmuted_audio_streams) {
            filter_spec += "[0:" + std::to_string(stream_index) + "]";
        }
        filter_spec += "amix=inputs=" + std::to_string(unmuted_audio_streams.size()) + "[a]";
        av_dict_set(&opts, "filter_complex", filter_spec.c_str(), 0);
        av_dict_set(&opts, "map", "0:v", 0);
        av_dict_set(&opts, "map", "[a]", 0);
    }
    
    // Write header
    ret = avformat_write_header(outFormatContext, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        // Error handling
        avio_closep(&outFormatContext->pb);
        avformat_free_context(outFormatContext);
        return false;
    }

    // Seek to start time
    ret = av_seek_frame(formatContext, -1, startTime * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
       // Error handling
       avformat_free_context(outFormatContext);
       return false;
    }

    AVPacket pkt;
    std::vector<int64_t> start_ts(formatContext->nb_streams, AV_NOPTS_VALUE);
    while (av_read_frame(formatContext, &pkt) >= 0) {
        double ts_seconds = pkt.pts * av_q2d(formatContext->streams[pkt.stream_index]->time_base);
        if (ts_seconds > endTime) {
            av_packet_unref(&pkt);
            break;
        }

        bool is_unmuted_audio = false;
        for (int stream_idx : unmuted_audio_streams) {
            if (pkt.stream_index == stream_idx) {
                is_unmuted_audio = true;
                break;
            }
        }

        if (pkt.stream_index != videoStreamIndex && !is_unmuted_audio) {
            av_packet_unref(&pkt);
            continue;
        }

        if (ts_seconds >= startTime) {
            if (start_ts[pkt.stream_index] == AV_NOPTS_VALUE) {
                start_ts[pkt.stream_index] = pkt.pts;
            }
            int64_t offset = start_ts[pkt.stream_index];

            AVStream* inStream = formatContext->streams[pkt.stream_index];
            AVStream* outStream;

            if (mergeAudio && is_unmuted_audio) {
                outStream = outFormatContext->streams[1]; // Merged audio stream
                pkt.stream_index = 1;
            } else if (pkt.stream_index == videoStreamIndex) {
                 outStream = outFormatContext->streams[0]; // Video stream
                 pkt.stream_index = 0;
            } else {
                int current_audio_stream = 0;
                for(size_t i = 0; i < unmuted_audio_streams.size(); ++i){
                    if(unmuted_audio_streams[i] == inStream->index){
                        current_audio_stream = i;
                        break;
                    }
                }
                outStream = outFormatContext->streams[1 + current_audio_stream];
                pkt.stream_index = 1 + current_audio_stream;
            }


            pkt.pts = av_rescale_q(pkt.pts - offset, inStream->time_base, outStream->time_base);
            if (pkt.dts != AV_NOPTS_VALUE)
                pkt.dts = av_rescale_q(pkt.dts - offset, inStream->time_base, outStream->time_base);
            if(pkt.dts > pkt.pts) pkt.dts = pkt.pts;
            pkt.duration = av_rescale_q(pkt.duration, inStream->time_base, outStream->time_base);
            pkt.pos = -1;

            ret = av_interleaved_write_frame(outFormatContext, &pkt);
            if (ret < 0) {
                // Error handling...
                break;
            }
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(outFormatContext);
    if (!(outFormatContext->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&outFormatContext->pb);
    }
    avformat_free_context(outFormatContext);

    // Reset format context for further use
    av_seek_frame(formatContext, -1, 0, AVSEEK_FLAG_BACKWARD);

    return true;
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
