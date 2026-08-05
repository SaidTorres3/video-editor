#include "audio_player.h"
#include "video_player.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <mmreg.h>
#include <ksmedia.h>

AudioPlayer::AudioPlayer(VideoPlayer* player) : m_player(player), m_framesWritten(0), m_masterVolume(1.0f) {}

AudioPlayer::~AudioPlayer() {
    Cleanup();
}

bool AudioPlayer::Initialize() {
    // Use multi-threaded COM so the audio client functions correctly from any thread
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE means COM is already initialized on this thread (e.g. as STA
    // by WinMain) — that is fine, WASAPI works from either apartment model.
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return false;
    m_comInitializedByUs = SUCCEEDED(hr);

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

    REFERENCE_TIME devicePeriod = 0;
    hr = m_player->audioClient->GetDevicePeriod(nullptr, &devicePeriod);
    if (FAILED(hr))
        devicePeriod = 100000; // fall back to 10ms
    // Video conversion and GPU readback can occasionally take more than one
    // device period.  A 100 ms shared-mode buffer absorbs that jitter without
    // forcing the decoder and WASAPI threads to run in lockstep.
    REFERENCE_TIME bufferDuration = std::max<REFERENCE_TIME>(devicePeriod * 6, 1000000);

    DWORD streamFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = m_player->audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, bufferDuration, 0, m_player->audioFormat, nullptr);
    if (FAILED(hr))
    {
        // Try with device format if our format fails
        CoTaskMemFree(m_player->audioFormat);
        m_player->audioFormat = deviceFormat;
        hr = m_player->audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, bufferDuration, 0, m_player->audioFormat, nullptr);
        if (FAILED(hr))
            return false;
    }
    else
    {
        CoTaskMemFree(deviceFormat);
    }

    // Update audio configuration to match the initialized format
    m_player->audioOutputIsFloat = false;
    if (m_player->audioFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        m_player->audioOutputIsFloat = true;
    }
    else if (m_player->audioFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
             m_player->audioFormat->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
    {
        auto *ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(m_player->audioFormat);
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
            m_player->audioOutputIsFloat = true;
    }
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
    std::lock_guard<std::mutex> lifecycleLock(m_threadLifecycleMutex);
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
    m_player->audioOutputIsFloat = false;
    
    m_player->audioInitialized = false;
    if (m_comInitializedByUs)
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
        // Cleanup voice isolation
        if (track->voiceIsolationBackSwrContext)
        {
            swr_free(&track->voiceIsolationBackSwrContext);
            track->voiceIsolationBackSwrContext = nullptr;
        }
        if (track->voiceIsolationSwrContext)
        {
            swr_free(&track->voiceIsolationSwrContext);
            track->voiceIsolationSwrContext = nullptr;
        }
        if (track->denoiseState)
        {
            rnnoise_destroy(track->denoiseState);
            track->denoiseState = nullptr;
        }
        track->voiceIsolationInputBuffer.clear();
        track->voiceIsolationMonoBuffer.clear();
        track->voiceIsolationProcessedBuffer.clear();
        track->voiceIsolationSampleQueue.clear();
        track->buffer.clear();
        track->bufferPts = 0.0;
    }
    m_player->audioTracks.clear();
}

void AudioPlayer::StartThread() {
    std::lock_guard<std::mutex> lifecycleLock(m_threadLifecycleMutex);
    // StartThread can be reached by the presentation thread while an
    // asynchronous seek is stopping/restarting playback. Never overwrite a
    // live std::thread: assigning to a joinable thread calls std::terminate.
    if (m_player->audioThreadRunning)
        return;
    if (m_player->audioThread.joinable())
        m_player->audioThread.join();

    if (!m_player->audioTracks.empty() && m_player->audioInitialized)
    {
        HRESULT hr = m_player->audioClient->Start();
        if (FAILED(hr))
        {
            // Continue without audio or handle error appropriately
        }
        m_framesWritten = 0;
        m_player->audioThreadRunning = true;
        m_player->audioThread = std::thread(&AudioPlayer::AudioThreadFunction, this);
    }
}

void AudioPlayer::StopThread(bool resetDevice) {
    std::lock_guard<std::mutex> lifecycleLock(m_threadLifecycleMutex);
    if (m_player->audioThreadRunning)
    {
        m_player->audioThreadRunning = false;
        m_player->audioCondition.notify_all();
        if (m_player->audioThread.joinable())
            m_player->audioThread.join();
    }
    if (resetDevice && m_player->audioClient)
        m_player->audioClient->Reset();
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
    if (framePts - m_player->startTimeOffset < 0.0)
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

    // Apply voice isolation if enabled
    {
        // Lock during voice isolation processing to avoid races with toggling/freeing
        std::lock_guard<std::mutex> isoLock(m_player->audioMutex);
        if (track->voiceIsolationEnabled && track->denoiseState &&
            track->voiceIsolationSwrContext && track->voiceIsolationBackSwrContext)
        {
            // Convert stereo audio to 48kHz mono for RNNoise processing
            int maxOutSamples = swr_get_out_samples(track->voiceIsolationSwrContext, convertedSamples);
            if (track->voiceIsolationMonoBuffer.size() < static_cast<size_t>(maxOutSamples))
                track->voiceIsolationMonoBuffer.resize(maxOutSamples);

            int16_t* monoPtr = track->voiceIsolationMonoBuffer.data();
            int convertedMono = swr_convert(track->voiceIsolationSwrContext,
                                           (uint8_t**)&monoPtr, maxOutSamples,
                                           (const uint8_t**)&outPtr, convertedSamples);

            if (convertedMono > 0)
            {
                // Add new samples to the queue
                for (int i = 0; i < convertedMono; ++i)
                {
                    track->voiceIsolationSampleQueue.push_back(static_cast<float>(monoPtr[i]));
                }

                int frameSize = rnnoise_get_frame_size(); // 480 samples
                int processedSamples = 0;

                // Ensure processed buffer is large enough
                if (track->voiceIsolationProcessedBuffer.size() < static_cast<size_t>(convertedMono))
                    track->voiceIsolationProcessedBuffer.resize(convertedMono);

                // Process complete 480-sample frames
                while (track->voiceIsolationSampleQueue.size() >= static_cast<size_t>(frameSize))
                {
                    // Fill input buffer with exactly 480 samples
                    for (int i = 0; i < frameSize; ++i)
                    {
                        track->voiceIsolationInputBuffer[i] = track->voiceIsolationSampleQueue.front();
                        track->voiceIsolationSampleQueue.pop_front();
                    }

                    // Apply RNNoise processing - this modifies the input buffer in place
                    rnnoise_process_frame(track->denoiseState,
                                          track->voiceIsolationInputBuffer.data(),
                                          track->voiceIsolationInputBuffer.data());

                    // Convert processed output back to int16 and store
                    for (int i = 0; i < frameSize; ++i)
                    {
                        float sample = track->voiceIsolationInputBuffer[i];
                        sample = std::max(-32768.0f, std::min(32767.0f, sample));
                        if (processedSamples + i < static_cast<int>(track->voiceIsolationProcessedBuffer.size()))
                        {
                            track->voiceIsolationProcessedBuffer[processedSamples + i] = static_cast<int16_t>(sample);
                        }
                    }
                    processedSamples += frameSize;
                }

                // Convert processed 48kHz mono back to original rate stereo
                if (processedSamples > 0)
                {
                    int16_t* processedPtr = track->voiceIsolationProcessedBuffer.data();
                    int backConverted = swr_convert(track->voiceIsolationBackSwrContext,
                                                   (uint8_t**)&outPtr, convertedSamples,
                                                   (const uint8_t**)&processedPtr, processedSamples);

                    if (backConverted > 0)
                    {
                        // The processed audio is now in outPtr, with the correct number of samples.
                        // We need to update convertedSamples to reflect the actual output size.
                        convertedSamples = backConverted;
                    }
                    // If conversion fails, the original audio remains in outPtr.
                }
            }
        }
    }

    // Store processed samples in track buffer
    {
        std::lock_guard<std::mutex> lock(m_player->audioMutex);
        if (track->buffer.empty())
            track->bufferPts = framePts - m_player->startTimeOffset;
        track->buffer.insert(track->buffer.end(),
                             outPtr,
                             outPtr + convertedSamples * m_player->audioChannels);
    }
    m_player->audioCondition.notify_one();
}

void AudioPlayer::SetMasterVolume(float volume) {
    constexpr float kMinAudibleAtMinus30Db = 0.03162278f; // 10^(-30/20)
    m_masterVolume = volume < 0.0f ? 0.0f : volume;
    if (m_masterVolume > 0.0f && m_masterVolume < kMinAudibleAtMinus30Db)
        m_masterVolume = 0.0f;
}

void AudioPlayer::AudioThreadFunction() {
    // Each thread interacting with WASAPI must initialize COM separately
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        return;
    if (!m_player->audioClient || !m_player->renderClient)
    {
        CoUninitialize();
        return;
    }

    HRESULT hr;
    auto startTime = m_player->masterStartTime;
    double startPts = m_player->masterStartPts;
    bool deviceStarted = false;

    // Starting an empty WASAPI client produces an immediate underrun.  Wait
    // for a modest preroll instead; video can begin rendering meanwhile.
    const int prerollFrames = std::min<int>(
        static_cast<int>(m_player->bufferFrameCount),
        std::max(1, m_player->audioSampleRate / 20)); // 50 ms
    const auto prerollDeadline = std::chrono::high_resolution_clock::now() +
                                 std::chrono::milliseconds(150);

    while (m_player->audioThreadRunning)
    {
        std::unique_lock<std::mutex> lock(m_player->audioMutex);
        if (!deviceStarted)
        {
            m_player->audioCondition.wait_until(lock, prerollDeadline, [this, startPts, prerollFrames] {
                return GetAvailableFrameCount(startPts) >= prerollFrames ||
                       !m_player->audioThreadRunning;
            });
        }
        else
        {
            m_player->audioCondition.wait_for(lock, std::chrono::milliseconds(2), [this] {
                return HasBufferedAudio() || !m_player->audioThreadRunning;
            });
        }

        if (!m_player->audioThreadRunning)
            break;

        UINT32 padding = 0;
        hr = m_player->audioClient->GetCurrentPadding(&padding);
        if (FAILED(hr))
            continue;

        double elapsed = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - startTime).count();
        UINT64 played = m_framesWritten > padding ? m_framesWritten - padding : 0;
        double playedSeconds = played / static_cast<double>(m_player->audioSampleRate);

        if (elapsed < playedSeconds)
        {
            lock.unlock();
            Sleep(1);
            continue;
        }

        const double deviceLead = m_player->bufferFrameCount /
                                  static_cast<double>(m_player->audioSampleRate);
        UINT64 targetWritten = static_cast<UINT64>((elapsed + deviceLead) * m_player->audioSampleRate);
        if (targetWritten < static_cast<UINT64>(m_framesWritten))
            targetWritten = static_cast<UINT64>(m_framesWritten);

        UINT32 framesNeeded = static_cast<UINT32>(targetWritten - m_framesWritten);

        UINT32 available = m_player->bufferFrameCount - padding;
        if (framesNeeded > available)
            framesNeeded = available;

        const double speed = m_player->GetPlaybackSpeed();
        const double outputPts = startPts +
                                 static_cast<double>(m_framesWritten) * speed /
                                 m_player->audioSampleRate;
        int buffered = GetAvailableFrameCount(outputPts);
        if (framesNeeded > static_cast<UINT32>(buffered))
            framesNeeded = static_cast<UINT32>(buffered);

        if (framesNeeded == 0)
        {
            lock.unlock();
            Sleep(1);
            continue;
        }

        BYTE* pData;
        hr = m_player->renderClient->GetBuffer(framesNeeded, &pData);
        if (FAILED(hr))
            continue;

        MixAudioTracks(pData, framesNeeded, outputPts);

        hr = m_player->renderClient->ReleaseBuffer(framesNeeded, 0);
        if (FAILED(hr))
            continue;

        m_framesWritten += framesNeeded;
        if (!deviceStarted)
        {
            hr = m_player->audioClient->Start();
            if (FAILED(hr))
                break;
            deviceStarted = true;
            startTime = std::chrono::high_resolution_clock::now();
        }
        lock.unlock();
    }

    if (deviceStarted)
        m_player->audioClient->Stop();
    CoUninitialize();
}

void AudioPlayer::MixAudioTracks(uint8_t* outputBuffer, int frameCount, double startPts) {
    const int channels = m_player->audioChannels;
    const bool outputIsFloat = m_player->audioOutputIsFloat;
    memset(outputBuffer, 0, static_cast<size_t>(frameCount) * m_player->audioFormat->nBlockAlign);
    int16_t *outInt16 = reinterpret_cast<int16_t*>(outputBuffer);
    float *outFloat = reinterpret_cast<float*>(outputBuffer);
    const double speed = m_player->GetPlaybackSpeed();
    const double sampleDuration = 1.0 / m_player->audioSampleRate;
    const bool normalSpeed = std::fabs(speed - 1.0) < 0.0001;
    std::vector<int32_t> mix(static_cast<size_t>(channels));

    for (int frame = 0; frame < frameCount; ++frame)
    {
        const double samplePts = startPts + frame * speed * sampleDuration;
        std::fill(mix.begin(), mix.end(), 0);
        for (auto& track : m_player->audioTracks)
        {
            if (track->isMuted)
                continue;

            // Drop samples that are earlier than the desired timestamp
            while (!track->buffer.empty() &&
                   track->bufferPts + sampleDuration <= samplePts)
            {
                for (int ch = 0; ch < channels && !track->buffer.empty(); ++ch)
                    track->buffer.pop_front();
                track->bufferPts += sampleDuration;
            }

            // Do not play a future packet early when there is a timestamp gap.
            if (track->bufferPts > samplePts + sampleDuration * 0.5 ||
                track->buffer.size() < static_cast<size_t>(channels))
                continue;

            if (normalSpeed)
            {
                for (int ch = 0; ch < channels; ++ch)
                {
                    const int16_t val = track->buffer.front();
                    track->buffer.pop_front();
                    mix[ch] += static_cast<int32_t>(val * track->volume * m_masterVolume);
                }
                track->bufferPts += sampleDuration;
            }
            else
            {
                // Linear interpolation avoids the harsh repeated/dropped-sample
                // edges produced by nearest-neighbour speed conversion.
                const double fraction = std::clamp(
                    (samplePts - track->bufferPts) / sampleDuration, 0.0, 1.0);
                const bool hasNext = track->buffer.size() >= static_cast<size_t>(channels * 2);
                for (int ch = 0; ch < channels; ++ch)
                {
                    const double first = track->buffer[ch];
                    const double second = hasNext ? track->buffer[channels + ch] : first;
                    const double value = first + (second - first) * fraction;
                    mix[ch] += static_cast<int32_t>(value * track->volume * m_masterVolume);
                }
            }
        }
        for (int ch = 0; ch < channels; ++ch)
        {
            int32_t v = mix[ch];
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;

            if (outputIsFloat)
                outFloat[frame * channels + ch] = static_cast<float>(v) / 32768.0f;
            else
                outInt16[frame * channels + ch] = static_cast<int16_t>(v);
        }
    }
}

bool AudioPlayer::HasBufferedAudio() const {
    for (const auto& track : m_player->audioTracks)
    {
        if (!track->isMuted && !track->buffer.empty())
            return true;
    }
    return false;
}

int AudioPlayer::GetAvailableFrameCount(double startPts) const {
    int maxFrames = 0;
    bool hasTrack = false;
    const int channels = std::max(1, m_player->audioChannels);
    const double sampleRate = std::max(1, m_player->audioSampleRate);
    const double speed = std::max(0.1, m_player->GetPlaybackSpeed());

    for (const auto& track : m_player->audioTracks)
    {
        if (track->isMuted)
            continue;

        hasTrack = true;
        int64_t sourceFrames = static_cast<int64_t>(track->buffer.size() / channels);
        if (sourceFrames <= 0)
            continue;

        if (startPts > track->bufferPts)
        {
            const int64_t staleFrames = static_cast<int64_t>(
                std::floor((startPts - track->bufferPts) * sampleRate + 1e-6));
            sourceFrames = std::max<int64_t>(0, sourceFrames - staleFrames);
        }
        if (sourceFrames <= 0)
            continue;

        int64_t outputFrames = sourceFrames;
        if (std::fabs(speed - 1.0) >= 0.0001)
            outputFrames = static_cast<int64_t>(std::floor((sourceFrames - 1) / speed)) + 1;

        outputFrames = std::min<int64_t>(outputFrames, INT_MAX);
        maxFrames = std::max(maxFrames, static_cast<int>(outputFrames));
    }
    if (!hasTrack)
        return 0;
    return maxFrames;
}
