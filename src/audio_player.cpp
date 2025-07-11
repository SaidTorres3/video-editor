#include "audio_player.h"
#include "video_player.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

using Clock = std::chrono::high_resolution_clock;

AudioPlayer::AudioPlayer(VideoPlayer* player)
    : m_player(player), m_framesWritten(0) {}

AudioPlayer::~AudioPlayer() {
    Cleanup();
}

// ------------------------------------------------------------
// Initialization and teardown
// ------------------------------------------------------------

bool AudioPlayer::Initialize() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return false;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&m_player->deviceEnumerator);
    if (FAILED(hr))
        return false;

    hr = m_player->deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_player->audioDevice);
    if (FAILED(hr))
        return false;

    hr = m_player->audioDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_player->audioClient);
    if (FAILED(hr))
        return false;

    // Desired output: 48kHz stereo float
    WAVEFORMATEX* deviceFormat = nullptr;
    hr = m_player->audioClient->GetMixFormat(&deviceFormat);
    if (FAILED(hr))
        return false;

    m_player->audioFormat = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
    m_player->audioFormat->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    m_player->audioFormat->nChannels = 2;
    m_player->audioFormat->nSamplesPerSec = 48000;
    m_player->audioFormat->wBitsPerSample = 32;
    m_player->audioFormat->nBlockAlign = (m_player->audioFormat->nChannels * m_player->audioFormat->wBitsPerSample) / 8;
    m_player->audioFormat->nAvgBytesPerSec = m_player->audioFormat->nSamplesPerSec * m_player->audioFormat->nBlockAlign;
    m_player->audioFormat->cbSize = 0;

    REFERENCE_TIME devicePeriod = 0;
    hr = m_player->audioClient->GetDevicePeriod(nullptr, &devicePeriod);
    if (FAILED(hr))
        devicePeriod = 100000; // 10ms
    REFERENCE_TIME bufferDuration = devicePeriod * 4; // ~40ms

    hr = m_player->audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufferDuration, 0, m_player->audioFormat, nullptr);
    if (FAILED(hr)) {
        // Fallback to device format if our ideal format is unsupported
        CoTaskMemFree(m_player->audioFormat);
        m_player->audioFormat = deviceFormat;
        hr = m_player->audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufferDuration, 0, m_player->audioFormat, nullptr);
        if (FAILED(hr))
            return false;
    } else {
        CoTaskMemFree(deviceFormat);
    }

    m_player->audioSampleRate = m_player->audioFormat->nSamplesPerSec;
    m_player->audioChannels = m_player->audioFormat->nChannels;
    m_player->audioSampleFormat = AV_SAMPLE_FMT_FLT;

    hr = m_player->audioClient->GetBufferSize(&m_player->bufferFrameCount);
    if (FAILED(hr))
        return false;

    hr = m_player->audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_player->renderClient);
    if (FAILED(hr))
        return false;

    m_player->audioInitialized = true;
    return true;
}

void AudioPlayer::Cleanup() {
    StopThread();

    if (m_player->renderClient) {
        m_player->renderClient->Release();
        m_player->renderClient = nullptr;
    }
    if (m_player->audioClient) {
        m_player->audioClient->Release();
        m_player->audioClient = nullptr;
    }
    if (m_player->audioDevice) {
        m_player->audioDevice->Release();
        m_player->audioDevice = nullptr;
    }
    if (m_player->deviceEnumerator) {
        m_player->deviceEnumerator->Release();
        m_player->deviceEnumerator = nullptr;
    }
    if (m_player->audioFormat) {
        CoTaskMemFree(m_player->audioFormat);
        m_player->audioFormat = nullptr;
    }

    m_player->audioInitialized = false;
    CoUninitialize();
}

// ------------------------------------------------------------
// Track management
// ------------------------------------------------------------

bool AudioPlayer::InitializeTracks() {
    if (!m_player->formatContext)
        return false;

    for (unsigned i = 0; i < m_player->formatContext->nb_streams; ++i) {
        if (m_player->formatContext->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;

        auto track = std::make_unique<AudioTrack>();
        track->streamIndex = i;

        AVCodecParameters* cp = m_player->formatContext->streams[i]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(cp->codec_id);
        if (!codec)
            continue;

        track->codecContext = avcodec_alloc_context3(codec);
        if (!track->codecContext)
            continue;

        if (avcodec_parameters_to_context(track->codecContext, cp) < 0) {
            avcodec_free_context(&track->codecContext);
            continue;
        }

        if (avcodec_open2(track->codecContext, codec, nullptr) < 0) {
            avcodec_free_context(&track->codecContext);
            continue;
        }

        track->frame = av_frame_alloc();
        if (!track->frame) {
            avcodec_free_context(&track->codecContext);
            continue;
        }

        track->swrContext = swr_alloc();
        if (!track->swrContext) {
            av_frame_free(&track->frame);
            avcodec_free_context(&track->codecContext);
            continue;
        }

        av_opt_set_int(track->swrContext, "in_channel_count", track->codecContext->ch_layout.nb_channels, 0);
        av_opt_set_int(track->swrContext, "out_channel_count", m_player->audioChannels, 0);
        av_opt_set_int(track->swrContext, "in_sample_rate", track->codecContext->sample_rate, 0);
        av_opt_set_int(track->swrContext, "out_sample_rate", m_player->audioSampleRate, 0);
        av_opt_set_sample_fmt(track->swrContext, "in_sample_fmt", track->codecContext->sample_fmt, 0);
        av_opt_set_sample_fmt(track->swrContext, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
        if (swr_init(track->swrContext) < 0) {
            swr_free(&track->swrContext);
            av_frame_free(&track->frame);
            avcodec_free_context(&track->codecContext);
            continue;
        }

        AVDictionaryEntry* title = av_dict_get(m_player->formatContext->streams[i]->metadata, "title", nullptr, 0);
        if (title)
            track->name = title->value;
        else
            track->name = "Audio Track " + std::to_string(m_player->audioTracks.size() + 1);

        m_player->audioTracks.push_back(std::move(track));
    }

    return !m_player->audioTracks.empty();
}

void AudioPlayer::CleanupTracks() {
    std::lock_guard<std::mutex> lock(m_player->audioMutex);
    for (auto& t : m_player->audioTracks) {
        if (t->swrContext)
            swr_free(&t->swrContext);
        if (t->frame)
            av_frame_free(&t->frame);
        if (t->codecContext)
            avcodec_free_context(&t->codecContext);
        t->buffer.clear();
        t->resampleBuffer.clear();
        t->bufferPts = 0.0;
    }
    m_player->audioTracks.clear();
}

// ------------------------------------------------------------
// Playback control
// ------------------------------------------------------------

void AudioPlayer::StartThread() {
    if (!m_player->audioInitialized || m_player->audioTracks.empty())
        return;

    if (m_player->audioClient)
        m_player->audioClient->Reset();

    HRESULT hr = m_player->audioClient->Start();
    if (FAILED(hr))
        return;

    m_framesWritten = 0;
    m_player->audioThreadRunning = true;
    m_player->audioThread = std::thread(&AudioPlayer::AudioThreadFunction, this);
}

void AudioPlayer::StopThread() {
    if (!m_player->audioThreadRunning)
        return;

    m_player->audioThreadRunning = false;
    m_player->audioCondition.notify_all();
    if (m_player->audioThread.joinable())
        m_player->audioThread.join();

    if (m_player->audioClient) {
        m_player->audioClient->Stop();
        m_player->audioClient->Reset();
    }

    std::lock_guard<std::mutex> lock(m_player->audioMutex);
    for (auto& t : m_player->audioTracks) {
        t->buffer.clear();
        t->bufferPts = 0.0;
    }
    m_framesWritten = 0;
}

// ------------------------------------------------------------
// Decoding and queuing audio frames
// ------------------------------------------------------------

void AudioPlayer::ProcessFrame(AVPacket* packet) {
    if (!m_player->audioInitialized)
        return;

    AudioTrack* track = nullptr;
    for (auto& t : m_player->audioTracks) {
        if (t->streamIndex == packet->stream_index) {
            track = t.get();
            break;
        }
    }
    if (!track || track->isMuted)
        return;

    if (avcodec_send_packet(track->codecContext, packet) < 0)
        return;

    while (avcodec_receive_frame(track->codecContext, track->frame) == 0) {
        AVStream* as = m_player->formatContext->streams[track->streamIndex];
        double pts = 0.0;
        if (track->frame->best_effort_timestamp != AV_NOPTS_VALUE)
            pts = track->frame->best_effort_timestamp * av_q2d(as->time_base);
        else if (track->frame->pts != AV_NOPTS_VALUE)
            pts = track->frame->pts * av_q2d(as->time_base);
        pts -= m_player->startTimeOffset;
        if (pts < 0.0)
            pts = 0.0;

        int outSamples = swr_get_out_samples(track->swrContext, track->frame->nb_samples);
        size_t needed = static_cast<size_t>(outSamples * m_player->audioChannels);
        if (track->resampleBuffer.size() < needed)
            track->resampleBuffer.resize(needed);

        float* out = track->resampleBuffer.data();
        int converted = swr_convert(track->swrContext, (uint8_t**)&out, outSamples,
                                    (const uint8_t**)track->frame->data, track->frame->nb_samples);
        if (converted <= 0)
            continue;

        std::lock_guard<std::mutex> lock(m_player->audioMutex);
        if (track->buffer.empty())
            track->bufferPts = pts;
        track->buffer.insert(track->buffer.end(), out, out + converted * m_player->audioChannels);
    }

    m_player->audioCondition.notify_one();
}

void AudioPlayer::SetMasterVolume(float volume) {
    float clamped = std::clamp(volume, 0.0f, 2.0f);
    for (auto& t : m_player->audioTracks)
        t->volume = clamped;
}

// ------------------------------------------------------------
// Audio mixing and playback thread
// ------------------------------------------------------------

void AudioPlayer::AudioThreadFunction() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        return;

    auto startTime = m_player->masterStartTime;
    double startPts = m_player->masterStartPts;
    HRESULT hr;

    while (m_player->audioThreadRunning) {
        std::unique_lock<std::mutex> lock(m_player->audioMutex);
        m_player->audioCondition.wait(lock, [this]{ return HasBufferedAudio() || !m_player->audioThreadRunning; });
        if (!m_player->audioThreadRunning)
            break;

        UINT32 padding = 0;
        hr = m_player->audioClient->GetCurrentPadding(&padding);
        if (FAILED(hr))
            continue;

        UINT32 available = m_player->bufferFrameCount - padding;
        if (available == 0) {
            lock.unlock();
            Sleep(1);
            continue;
        }

        double elapsed = std::chrono::duration<double>(Clock::now() - startTime).count();
        double masterPts = startPts + elapsed;
        UINT64 played = m_framesWritten > padding ? m_framesWritten - padding : 0;
        double playedPts = played / static_cast<double>(m_player->audioSampleRate);
        if (masterPts < playedPts - 0.02) {
            lock.unlock();
            Sleep(1);
            continue;
        }

        BYTE* data = nullptr;
        hr = m_player->renderClient->GetBuffer(available, &data);
        if (FAILED(hr))
            continue;

        float* out = reinterpret_cast<float*>(data);
        double outPts = startPts + static_cast<double>(m_framesWritten) / m_player->audioSampleRate;
        MixAudioTracks(out, available, outPts);

        hr = m_player->renderClient->ReleaseBuffer(available, 0);
        if (FAILED(hr))
            continue;

        m_framesWritten += available;
        lock.unlock();
    }

    if (m_player->audioClient)
        m_player->audioClient->Stop();
    CoUninitialize();
}

void AudioPlayer::MixAudioTracks(float* out, int frames, double startPts) {
    int channels = m_player->audioChannels;
    std::fill(out, out + frames * channels, 0.0f);

    for (auto& tptr : m_player->audioTracks) {
        auto& track = *tptr;
        if (track.isMuted || track.buffer.empty())
            continue;

        // Remove samples earlier than startPts
        double diff = startPts - track.bufferPts;
        if (diff > 0) {
            size_t drop = static_cast<size_t>(diff * m_player->audioSampleRate * channels);
            drop = std::min(drop, track.buffer.size());
            track.buffer.erase(track.buffer.begin(), track.buffer.begin() + drop);
            track.bufferPts += static_cast<double>(drop) / (channels * m_player->audioSampleRate);
            if (track.buffer.empty())
                continue;
        }

        size_t needed = static_cast<size_t>(frames * channels);
        size_t avail = std::min(needed, track.buffer.size());
        for (size_t i = 0; i < avail; ++i)
            out[i] += track.buffer[i] * track.volume;
        track.buffer.erase(track.buffer.begin(), track.buffer.begin() + avail);
        track.bufferPts += static_cast<double>(avail) / (channels * m_player->audioSampleRate);
    }

    for (int i = 0; i < frames * channels; ++i) {
        float v = out[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        out[i] = v;
    }
}

bool AudioPlayer::HasBufferedAudio() const {
    for (const auto& t : m_player->audioTracks) {
        if (!t->buffer.empty())
            return true;
    }
    return false;
}

