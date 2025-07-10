#include "audio_player.h"
#include "video_player.h"
#include <cmath>

AudioPlayer::AudioPlayer(VideoPlayer* player)
    : m_player(player), m_running(false) {}

AudioPlayer::~AudioPlayer() {
    Cleanup();
}

bool AudioPlayer::Initialize() {
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr))
        return false;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&m_player->deviceEnumerator));
    if (FAILED(hr))
        return false;

    hr = m_player->deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                             &m_player->audioDevice);
    if (FAILED(hr))
        return false;

    hr = m_player->audioDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                         reinterpret_cast<void**>(&m_player->audioClient));
    if (FAILED(hr))
        return false;

    WAVEFORMATEX* mixFormat = nullptr;
    hr = m_player->audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr))
        return false;

    // Force 32-bit float output
    WAVEFORMATEXTENSIBLE* fmtEx = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mixFormat);
    fmtEx->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    fmtEx->Format.wBitsPerSample = 32;
    fmtEx->Format.nBlockAlign = fmtEx->Format.nChannels * 4;
    fmtEx->Format.nAvgBytesPerSec = fmtEx->Format.nBlockAlign * fmtEx->Format.nSamplesPerSec;
    fmtEx->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    fmtEx->Samples.wValidBitsPerSample = 32;
    fmtEx->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    REFERENCE_TIME bufferTime = 1000000; // 100ms
    hr = m_player->audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                           AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                           bufferTime, 0, mixFormat, nullptr);
    if (FAILED(hr)) {
        CoTaskMemFree(mixFormat);
        return false;
    }

    hr = m_player->audioClient->GetService(__uuidof(IAudioRenderClient),
                                           reinterpret_cast<void**>(&m_player->renderClient));
    if (FAILED(hr)) {
        CoTaskMemFree(mixFormat);
        return false;
    }

    hr = m_player->audioClient->GetBufferSize(&m_player->bufferFrameCount);
    if (FAILED(hr)) {
        CoTaskMemFree(mixFormat);
        return false;
    }

    m_player->audioFormat = mixFormat;
    m_player->audioSampleRate = mixFormat->nSamplesPerSec;
    m_player->audioChannels = mixFormat->nChannels;
    m_player->audioSampleFormat = AV_SAMPLE_FMT_FLT;

    m_player->audioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_player->audioEvent)
        return false;
    hr = m_player->audioClient->SetEventHandle(m_player->audioEvent);
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
    if (m_player->audioEvent) {
        CloseHandle(m_player->audioEvent);
        m_player->audioEvent = nullptr;
    }
    m_player->audioInitialized = false;
    CoUninitialize();
}

bool AudioPlayer::InitializeTracks() {
    if (!m_player->formatContext)
        return false;

    for (unsigned i = 0; i < m_player->formatContext->nb_streams; ++i) {
        if (m_player->formatContext->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;

        auto track = std::make_unique<AudioTrack>();
        track->streamIndex = i;

        AVCodecParameters* codecpar = m_player->formatContext->streams[i]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec)
            continue;
        track->codecContext = avcodec_alloc_context3(codec);
        if (!track->codecContext)
            continue;
        if (avcodec_parameters_to_context(track->codecContext, codecpar) < 0) {
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

        AVChannelLayout in_ch, out_ch;
        av_channel_layout_default(&in_ch, track->codecContext->ch_layout.nb_channels);
        av_channel_layout_default(&out_ch, m_player->audioChannels);
        av_opt_set_chlayout(track->swrContext, "in_chlayout", &in_ch, 0);
        av_opt_set_chlayout(track->swrContext, "out_chlayout", &out_ch, 0);
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

        AVDictionaryEntry* title = av_dict_get(m_player->formatContext->streams[i]->metadata,
                                               "title", nullptr, 0);
        track->name = title ? title->value : "Audio Track " + std::to_string(m_player->audioTracks.size() + 1);

        m_player->audioTracks.push_back(std::move(track));
    }

    return !m_player->audioTracks.empty();
}

void AudioPlayer::CleanupTracks() {
    for (auto& t : m_player->audioTracks) {
        if (t->swrContext)
            swr_free(&t->swrContext);
        if (t->frame)
            av_frame_free(&t->frame);
        if (t->codecContext)
            avcodec_free_context(&t->codecContext);
    }
    m_player->audioTracks.clear();
}

void AudioPlayer::StartThread() {
    if (!m_player->audioInitialized || m_player->audioTracks.empty())
        return;

    if (FAILED(m_player->audioClient->Start()))
        return;

    m_running = true;
    m_thread = std::thread(&AudioPlayer::AudioThread, this);
}

void AudioPlayer::StopThread() {
    if (!m_running)
        return;
    m_running = false;
    if (m_player->audioEvent)
        SetEvent(m_player->audioEvent);
    if (m_thread.joinable())
        m_thread.join();
    if (m_player->audioClient)
        m_player->audioClient->Stop();
}

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
    if (avcodec_receive_frame(track->codecContext, track->frame) < 0)
        return;

    AVStream* as = m_player->formatContext->streams[track->streamIndex];
    double pts = 0.0;
    if (track->frame->best_effort_timestamp != AV_NOPTS_VALUE)
        pts = track->frame->best_effort_timestamp * av_q2d(as->time_base);
    else if (track->frame->pts != AV_NOPTS_VALUE)
        pts = track->frame->pts * av_q2d(as->time_base);
    pts -= m_player->startTimeOffset;
    if (pts < 0.0)
        return;

    int outSamples = swr_get_out_samples(track->swrContext, track->frame->nb_samples);
    size_t needed = static_cast<size_t>(outSamples * m_player->audioChannels);
    if (track->resampleBuffer.size() < needed)
        track->resampleBuffer.resize(needed);
    float* outPtr = track->resampleBuffer.data();
    int converted = swr_convert(track->swrContext, reinterpret_cast<uint8_t**>(&outPtr), outSamples,
                                const_cast<const uint8_t**>(track->frame->data), track->frame->nb_samples);
    if (converted <= 0)
        return;

    std::lock_guard<std::mutex> lock(m_player->audioMutex);
    if (track->buffer.empty())
        track->nextPts = pts;
    track->buffer.insert(track->buffer.end(), outPtr, outPtr + converted * m_player->audioChannels);
}

void AudioPlayer::SetMasterVolume(float volume) {
    float v = volume < 0.f ? 0.f : (volume > 2.f ? 2.f : volume);
    for (auto& t : m_player->audioTracks)
        t->volume = v;
}

void AudioPlayer::AudioThread() {
    HANDLE evt = m_player->audioEvent;
    while (m_running) {
        if (WaitForSingleObject(evt, INFINITE) != WAIT_OBJECT_0)
            continue;
        if (!m_running)
            break;
        UINT32 padding = 0;
        if (FAILED(m_player->audioClient->GetCurrentPadding(&padding)))
            continue;
        UINT32 frames = m_player->bufferFrameCount - padding;
        if (frames == 0)
            continue;
        BYTE* data = nullptr;
        if (FAILED(m_player->renderClient->GetBuffer(frames, &data)))
            continue;
        Mix(reinterpret_cast<float*>(data), frames);
        m_player->renderClient->ReleaseBuffer(frames, 0);
    }
}

void AudioPlayer::Mix(float* output, UINT32 frames) {
    std::fill(output, output + frames * m_player->audioChannels, 0.f);
    double baseTime = m_player->GetSyncTime();
    double rate = static_cast<double>(m_player->audioSampleRate);
    const double eps = 0.001; // 1ms tolerance

    for (UINT32 f = 0; f < frames; ++f) {
        double t = baseTime + static_cast<double>(f) / rate;
        for (auto& tr : m_player->audioTracks) {
            if (tr->isMuted)
                continue;
            while (tr->buffer.size() >= static_cast<size_t>(m_player->audioChannels) &&
                   tr->nextPts < t - eps) {
                for (int c = 0; c < m_player->audioChannels; ++c)
                    tr->buffer.pop_front();
                tr->nextPts += 1.0 / rate;
            }
            if (tr->buffer.size() >= static_cast<size_t>(m_player->audioChannels) &&
                std::fabs(tr->nextPts - t) <= eps) {
                for (int c = 0; c < m_player->audioChannels; ++c) {
                    float sample = tr->buffer.front();
                    tr->buffer.pop_front();
                    output[f * m_player->audioChannels + c] += sample * tr->volume;
                }
                tr->nextPts += 1.0 / rate;
            }
        }
    }

    for (UINT32 i = 0; i < frames * m_player->audioChannels; ++i) {
        float& v = output[i];
        if (v > 1.f) v = 1.f;
        if (v < -1.f) v = -1.f;
    }
}

bool AudioPlayer::TracksHaveData() const {
    for (const auto& t : m_player->audioTracks)
        if (!t->buffer.empty())
            return true;
    return false;
}
