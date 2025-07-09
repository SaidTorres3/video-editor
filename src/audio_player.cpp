#include "audio_player.h"
#include "video_player.h"

AudioPlayer::AudioPlayer(VideoPlayer* player) : m_player(player) {}

AudioPlayer::~AudioPlayer() {
    Cleanup();
}

bool AudioPlayer::Initialize() {
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

    // Get the default audio format
    WAVEFORMATEX *deviceFormat = nullptr;
    hr = m_player->audioClient->GetMixFormat(&deviceFormat);
    if (FAILED(hr))
        return false;

    // Set up our desired format (16-bit stereo at 44.1kHz)
    m_player->audioFormat = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
    m_player->audioFormat->wFormatTag = WAVE_FORMAT_PCM;
    m_player->audioFormat->nChannels = 2;
    m_player->audioFormat->nSamplesPerSec = 44100;
    m_player->audioFormat->wBitsPerSample = 16;
    m_player->audioFormat->nBlockAlign = (m_player->audioFormat->nChannels * m_player->audioFormat->wBitsPerSample) / 8;
    m_player->audioFormat->nAvgBytesPerSec = m_player->audioFormat->nSamplesPerSec * m_player->audioFormat->nBlockAlign;
    m_player->audioFormat->cbSize = 0;

    // Use a smaller buffer duration (100ms) to reduce playback latency
    hr = m_player->audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, m_player->audioFormat, nullptr);
    if (FAILED(hr))
    {
        // Try with device format if our format fails
        CoTaskMemFree(m_player->audioFormat);
        m_player->audioFormat = deviceFormat;
        hr = m_player->audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, m_player->audioFormat, nullptr);
        if (FAILED(hr))
            return false;
    }
    else
    {
        CoTaskMemFree(deviceFormat);
    }

    // Update audio configuration to match the initialized format
    m_player->audioSampleRate = m_player->audioFormat->nSamplesPerSec;
    m_player->audioChannels = m_player->audioFormat->nChannels;

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
    if (m_player->audioThreadRunning)
    {
        m_player->audioThreadRunning = false;
        m_player->audioCondition.notify_all();
        if (m_player->audioThread.joinable())
            m_player->audioThread.join();
    }

    if (m_player->renderClient)
    {
        m_player->renderClient->Release();
        m_player->renderClient = nullptr;
    }
    if (m_player->audioClient)
    {
        m_player->audioClient->Release();
        m_player->audioClient = nullptr;
    }
    if (m_player->audioDevice)
    {
        m_player->audioDevice->Release();
        m_player->audioDevice = nullptr;
    }
    if (m_player->deviceEnumerator)
    {
        m_player->deviceEnumerator->Release();
        m_player->deviceEnumerator = nullptr;
    }
    if (m_player->audioFormat)
    {
        CoTaskMemFree(m_player->audioFormat);
        m_player->audioFormat = nullptr;
    }
    
    m_player->audioInitialized = false;
    CoUninitialize();
}

bool AudioPlayer::InitializeTracks() {
    if (!m_player->formatContext)
        return false;

    // Find all audio streams
    for (unsigned i = 0; i < m_player->formatContext->nb_streams; i++)
    {
        if (m_player->formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            auto track = std::make_unique<AudioTrack>();
            track->streamIndex = i;
            
            // Get codec and create context
            AVCodecParameters *codecpar = m_player->formatContext->streams[i]->codecpar;
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
            av_opt_set_int(track->swrContext, "out_sample_rate", m_player->audioSampleRate, 0);
            av_opt_set_sample_fmt(track->swrContext, "in_sample_fmt", track->codecContext->sample_fmt, 0);
            av_opt_set_sample_fmt(track->swrContext, "out_sample_fmt", m_player->audioSampleFormat, 0);

            if (swr_init(track->swrContext) < 0)
            {
                swr_free(&track->swrContext);
                av_frame_free(&track->frame);
                avcodec_free_context(&track->codecContext);
                continue;
            }

            // Record track start time for synchronization
            if (m_player->formatContext->streams[i]->start_time != AV_NOPTS_VALUE)
                track->startTime = m_player->formatContext->streams[i]->start_time *
                                 av_q2d(m_player->formatContext->streams[i]->time_base);
            else
                track->startTime = 0.0;

            // Set track name
            AVDictionaryEntry *title = av_dict_get(m_player->formatContext->streams[i]->metadata, "title", nullptr, 0);
            if (title)
                track->name = title->value;
            else
                track->name = "Audio Track " + std::to_string(m_player->audioTracks.size() + 1);

            m_player->audioTracks.push_back(std::move(track));
        }
    }

    return !m_player->audioTracks.empty();
}

void AudioPlayer::CleanupTracks() {
    for (auto& track : m_player->audioTracks)
    {
        if (track->swrContext)
            swr_free(&track->swrContext);
        if (track->frame)
            av_frame_free(&track->frame);
        if (track->codecContext)
            avcodec_free_context(&track->codecContext);
    }
    m_player->audioTracks.clear();
}

void AudioPlayer::StartThread() {
    if (!m_player->audioTracks.empty() && m_player->audioInitialized)
    {
        HRESULT hr = m_player->audioClient->Start();
        if (FAILED(hr))
        {
            // Continue without audio or handle error appropriately
        }
        m_player->audioThreadRunning = true;
        m_player->audioThread = std::thread(&AudioPlayer::AudioThreadFunction, this);
    }
}

void AudioPlayer::StopThread() {
    if (m_player->audioThreadRunning)
    {
        m_player->audioThreadRunning = false;
        m_player->audioCondition.notify_all();
        if (m_player->audioThread.joinable())
            m_player->audioThread.join();
    }
}

void AudioPlayer::ProcessFrame(AVPacket* audioPacket) {
    if (!m_player->audioInitialized || m_player->audioTracks.empty())
        return;

    // Find the corresponding audio track
    AudioTrack *track = nullptr;
    for (auto& t : m_player->audioTracks)
    {
        if (t->streamIndex == audioPacket->stream_index)
        {
            track = t.get();
            break;
        }
    }

    if (!track || track->isMuted)
        return;

    // Decode audio frame
    int ret = avcodec_send_packet(track->codecContext, audioPacket);
    if (ret < 0)
        return;

    ret = avcodec_receive_frame(track->codecContext, track->frame);
    if (ret < 0)
        return;

    AVStream *as = m_player->formatContext->streams[track->streamIndex];
    double framePts = 0.0;
    if (track->frame->best_effort_timestamp != AV_NOPTS_VALUE)
        framePts = track->frame->best_effort_timestamp * av_q2d(as->time_base);
    else if (track->frame->pts != AV_NOPTS_VALUE)
        framePts = track->frame->pts * av_q2d(as->time_base);

    framePts -= m_player->startTimeOffset;
    if (framePts < 0.0)
        return; // Drop early audio

    // Resample audio
    int outSamples = swr_get_out_samples(track->swrContext, track->frame->nb_samples);
    size_t needed = static_cast<size_t>(outSamples * m_player->audioChannels);
    if (track->resampleBuffer.size() < needed)
        track->resampleBuffer.resize(needed);
    int16_t* outPtr = track->resampleBuffer.data();

    int convertedSamples = swr_convert(track->swrContext, (uint8_t**)&outPtr, outSamples,
                                        (const uint8_t**)track->frame->data, track->frame->nb_samples);
    if (convertedSamples < 0)
        return;

    // Store samples with timestamps for synchronization
    {
        std::lock_guard<std::mutex> lock(m_player->audioMutex);
        double sampleDuration = 1.0 / m_player->audioSampleRate;
        for (int i = 0; i < convertedSamples; ++i) {
            AudioSample s;
            s.timestamp = framePts + i * sampleDuration;
            s.left = outPtr[i * m_player->audioChannels];
            if (m_player->audioChannels > 1)
                s.right = outPtr[i * m_player->audioChannels + 1];
            else
                s.right = s.left;
            track->timedBuffer.push_back(s);
        }
    }
    m_player->audioCondition.notify_one();
}

void AudioPlayer::SetMasterVolume(float volume) {
    float clampedVolume = volume < 0.0f ? 0.0f : (volume > 2.0f ? 2.0f : volume);
    for (auto& track : m_player->audioTracks)
    {
        track->volume = clampedVolume;
    }
}

void AudioPlayer::AudioThreadFunction() {
    if (!m_player->audioClient || !m_player->renderClient)
        return;

    HRESULT hr; // Declare hr here

    while (m_player->audioThreadRunning)
    {
        std::unique_lock<std::mutex> lock(m_player->audioMutex);
        m_player->audioCondition.wait(lock, [this] { return HasBufferedAudio() || !m_player->audioThreadRunning; });

        if (!m_player->audioThreadRunning)
            break;

        if (!HasBufferedAudio())
            continue;

        // Get available buffer space
        UINT32 numFramesPadding;
        hr = m_player->audioClient->GetCurrentPadding(&numFramesPadding);
        if (FAILED(hr))
            continue;

        UINT32 numFramesAvailable = m_player->bufferFrameCount - numFramesPadding;
        if (numFramesAvailable == 0)
        {
            lock.unlock();
            Sleep(1);
            continue;
        }

        // Get render buffer
        BYTE *pData;
        hr = m_player->renderClient->GetBuffer(numFramesAvailable, &pData);
        if (FAILED(hr))
            continue;

        // Mix audio from all tracks using master clock
        double masterTime = m_player->GetMasterClockTime();
        double frameDuration = 1.0 / m_player->audioSampleRate;

        // Account for existing samples already queued in the device
        double latency = static_cast<double>(numFramesPadding) / m_player->audioSampleRate;

        MixSynchronizedAudio(pData, numFramesAvailable, masterTime + latency, frameDuration);

        hr = m_player->renderClient->ReleaseBuffer(numFramesAvailable, 0);
        if (FAILED(hr))
            continue;

        lock.unlock();
    }

    m_player->audioClient->Stop();
}

void AudioPlayer::MixAudioTracks(uint8_t* outputBuffer, int frameCount) {
    double masterTime = m_player->GetMasterClockTime();
    double frameDuration = 1.0 / m_player->audioSampleRate;
    MixSynchronizedAudio(outputBuffer, frameCount, masterTime, frameDuration);
}

void AudioPlayer::MixSynchronizedAudio(uint8_t* outputBuffer, int frameCount,
                                       double startTime, double frameDuration) {
    memset(outputBuffer, 0, frameCount * m_player->audioChannels * sizeof(int16_t));
    int16_t *out = reinterpret_cast<int16_t*>(outputBuffer);

    for (int frame = 0; frame < frameCount; ++frame) {
        double targetTime = startTime + frame * frameDuration;
        std::vector<int32_t> mix(m_player->audioChannels, 0);
        for (auto& track : m_player->audioTracks) {
            if (track->isMuted || track->timedBuffer.empty())
                continue;

            if (track->timedBuffer.front().timestamp <= targetTime) {
                AudioSample s = track->timedBuffer.front();
                track->timedBuffer.pop_front();
                mix[0] += static_cast<int32_t>(s.left * track->volume);
                if (m_player->audioChannels > 1)
                    mix[1] += static_cast<int32_t>(s.right * track->volume);
            }
        }
        for (int ch = 0; ch < m_player->audioChannels; ++ch) {
            int32_t v = mix[ch];
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[frame * m_player->audioChannels + ch] = static_cast<int16_t>(v);
        }
    }
}

bool AudioPlayer::HasBufferedAudio() const {
    for (const auto& track : m_player->audioTracks) {
        if (!track->timedBuffer.empty())
            return true;
    }
    return false;
}
