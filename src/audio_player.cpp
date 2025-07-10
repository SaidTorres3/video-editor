#include "audio_player.h"
#include "audio_mixer.h"
#include "video_player.h"
#include <chrono>
#include <vector>
#include <memory>
#include <cmath>
#include <cstring>
#include <mmreg.h>

using namespace std::chrono;

AudioPlayer::AudioPlayer(VideoPlayer* player)
    : m_player(player), m_mixer(nullptr), m_nextAudioPts(0.0), m_outputFloat(false) {}

AudioPlayer::~AudioPlayer()
{
    Cleanup();
}

bool AudioPlayer::Initialize()
{
    HRESULT hr = CoInitialize(nullptr);
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

    WAVEFORMATEX* deviceFormat = nullptr;
    hr = m_player->audioClient->GetMixFormat(&deviceFormat);
    if (FAILED(hr))
        return false;

    m_player->audioFormat = deviceFormat;

    m_outputFloat = (m_player->audioFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (m_player->audioFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(m_player->audioFormat);
        if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            m_outputFloat = true;
    }

    hr = m_player->audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, m_player->audioFormat, nullptr);
    if (FAILED(hr))
        return false;

    m_player->audioSampleRate = m_player->audioFormat->nSamplesPerSec;
    m_player->audioChannels = m_player->audioFormat->nChannels;
    m_player->audioSampleFormat = AV_SAMPLE_FMT_FLT; // internal mixing format

    hr = m_player->audioClient->GetBufferSize(&m_player->bufferFrameCount);
    if (FAILED(hr))
        return false;

    hr = m_player->audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_player->renderClient);
    if (FAILED(hr))
        return false;

    m_mixer = std::make_unique<AudioMixer>(m_player->audioSampleRate, m_player->audioChannels);

    m_player->audioInitialized = true;
    return true;
}

void AudioPlayer::Cleanup()
{
    StopThread();
    if (m_player->renderClient)
        m_player->renderClient->Release(), m_player->renderClient = nullptr;
    if (m_player->audioClient)
        m_player->audioClient->Release(), m_player->audioClient = nullptr;
    if (m_player->audioDevice)
        m_player->audioDevice->Release(), m_player->audioDevice = nullptr;
    if (m_player->deviceEnumerator)
        m_player->deviceEnumerator->Release(), m_player->deviceEnumerator = nullptr;
    if (m_player->audioFormat)
        CoTaskMemFree(m_player->audioFormat), m_player->audioFormat = nullptr;
    m_player->audioInitialized = false;
    CoUninitialize();
}

bool AudioPlayer::InitializeTracks()
{
    if (!m_player->formatContext)
        return false;

    for (unsigned i = 0; i < m_player->formatContext->nb_streams; ++i)
    {
        AVStream* stream = m_player->formatContext->streams[i];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;

        auto track = std::make_unique<AudioTrack>();
        track->streamIndex = i;

        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec)
            continue;
        track->codecContext = avcodec_alloc_context3(codec);
        if (!track->codecContext)
            continue;
        if (avcodec_parameters_to_context(track->codecContext, stream->codecpar) < 0)
        {
            avcodec_free_context(&track->codecContext);
            continue;
        }
        if (avcodec_open2(track->codecContext, codec, nullptr) < 0)
        {
            avcodec_free_context(&track->codecContext);
            continue;
        }
        track->frame = av_frame_alloc();
        if (!track->frame)
        {
            avcodec_free_context(&track->codecContext);
            continue;
        }

        AVChannelLayout inLayout{};
        av_channel_layout_default(&inLayout,
                                  track->codecContext->ch_layout.nb_channels > 0 ?
                                      track->codecContext->ch_layout.nb_channels : 2);
        AVChannelLayout outLayout{};
        av_channel_layout_default(&outLayout, m_player->audioChannels);

        track->swrContext = swr_alloc();
        if (track->swrContext)
        {
            av_opt_set_chlayout(track->swrContext, "in_chlayout", &inLayout, 0);
            av_opt_set_chlayout(track->swrContext, "out_chlayout", &outLayout, 0);
            av_opt_set_int(track->swrContext, "in_sample_rate", track->codecContext->sample_rate, 0);
            av_opt_set_int(track->swrContext, "out_sample_rate", m_player->audioSampleRate, 0);
            av_opt_set_sample_fmt(track->swrContext, "in_sample_fmt", track->codecContext->sample_fmt, 0);
            av_opt_set_sample_fmt(track->swrContext, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
        }

        if (!track->swrContext || swr_init(track->swrContext) < 0)
        {
            if (track->swrContext) swr_free(&track->swrContext);
            av_frame_free(&track->frame);
            avcodec_free_context(&track->codecContext);
            continue;
        }

        AVDictionaryEntry* title = av_dict_get(stream->metadata, "title", nullptr, 0);
        track->name = title ? title->value : "Audio Track " + std::to_string(m_player->audioTracks.size() + 1);

        m_player->audioTracks.push_back(std::move(track));
    }

    return !m_player->audioTracks.empty();
}

void AudioPlayer::CleanupTracks()
{
    for (auto& track : m_player->audioTracks)
    {
        if (track->swrContext)
            swr_free(&track->swrContext);
        if (track->frame)
            av_frame_free(&track->frame);
        if (track->codecContext)
            avcodec_free_context(&track->codecContext);
        track->buffer.clear();
    }
    m_player->audioTracks.clear();
}

void AudioPlayer::StartThread()
{
    if (!m_player->audioInitialized || m_player->audioTracks.empty())
        return;

    m_nextAudioPts = m_player->masterStartPts;
    HRESULT hr = m_player->audioClient->Start();
    if (FAILED(hr))
        return;

    m_player->audioThreadRunning = true;
    m_player->audioThread = std::thread(&AudioPlayer::AudioThreadFunction, this);
}

void AudioPlayer::StopThread()
{
    if (m_player->audioThreadRunning)
    {
        m_player->audioThreadRunning = false;
        if (m_player->audioThread.joinable())
            m_player->audioThread.join();
        if (m_player->audioClient)
            m_player->audioClient->Stop();
    }
}

void AudioPlayer::ProcessFrame(AVPacket* packet)
{
    if (!m_player->audioInitialized)
        return;

    AudioTrack* track = nullptr;
    for (auto& t : m_player->audioTracks)
    {
        if (t->streamIndex == packet->stream_index)
        {
            track = t.get();
            break;
        }
    }
    if (!track || track->isMuted)
        return;

    if (avcodec_send_packet(track->codecContext, packet) < 0)
        return;

    while (avcodec_receive_frame(track->codecContext, track->frame) == 0)
    {
        AVStream* as = m_player->formatContext->streams[track->streamIndex];
        double pts = track->frame->best_effort_timestamp != AV_NOPTS_VALUE ?
            track->frame->best_effort_timestamp * av_q2d(as->time_base) : m_nextAudioPts;
        pts -= m_player->startTimeOffset;

        int outSamples = swr_get_out_samples(track->swrContext, track->frame->nb_samples);
        if ((int)track->resampleBuffer.size() < outSamples * m_player->audioChannels)
            track->resampleBuffer.resize(outSamples * m_player->audioChannels);
        float* out = track->resampleBuffer.data();
        int converted = swr_convert(track->swrContext, (uint8_t**)&out, outSamples,
                                    (const uint8_t**)track->frame->data, track->frame->nb_samples);
        if (converted <= 0)
            continue;

        std::lock_guard<std::mutex> lk(m_player->audioMutex);
        if (track->buffer.empty())
            track->nextPts = pts;
        track->buffer.insert(track->buffer.end(), out, out + converted * m_player->audioChannels);
    }
}

void AudioPlayer::SetMasterVolume(float volume)
{
    float vol = volume < 0.f ? 0.f : (volume > 2.f ? 2.f : volume);
    for (auto& t : m_player->audioTracks)
        t->volume = vol;
}

void AudioPlayer::AudioThreadFunction()
{
    const double sampleDur = 1.0 / m_player->audioSampleRate;
    std::vector<float> mixBuffer(m_player->bufferFrameCount * m_player->audioChannels);

    while (m_player->audioThreadRunning)
    {
        UINT32 padding = 0;
        if (FAILED(m_player->audioClient->GetCurrentPadding(&padding)))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        UINT32 available = m_player->bufferFrameCount - padding;
        if (available == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        float* out = mixBuffer.data();
        double pts = m_nextAudioPts;
        {
            std::lock_guard<std::mutex> lk(m_player->audioMutex);
            m_mixer->Mix(m_player->audioTracks, out, available, pts);
        }

        BYTE* pData = nullptr;
        if (FAILED(m_player->renderClient->GetBuffer(available, &pData)))
            continue;

        if (m_outputFloat) {
            float* dest = reinterpret_cast<float*>(pData);
            for (UINT32 i = 0; i < available * m_player->audioChannels; ++i)
                dest[i] = out[i];
        } else {
            int16_t* dest = reinterpret_cast<int16_t*>(pData);
            for (UINT32 i = 0; i < available * m_player->audioChannels; ++i)
                dest[i] = static_cast<int16_t>(std::lrintf(out[i] * 32767.f));
        }

        m_player->renderClient->ReleaseBuffer(available, 0);
        m_nextAudioPts += available * sampleDur;
    }
}
