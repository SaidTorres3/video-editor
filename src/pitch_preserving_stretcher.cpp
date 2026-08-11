#include "pitch_preserving_stretcher.h"

#include <algorithm>
#include <rubberband/RubberBandStretcher.h>

namespace
{
constexpr size_t kMaxProcessFrames = 4096;
}

PitchPreservingStretcher::PitchPreservingStretcher(
    int sampleRate, int channels, double speed)
    : m_channels(std::max(1, channels)),
      m_maxProcessFrames(kMaxProcessFrames),
      m_startDelayRemaining(0)
{
    using Stretcher = RubberBand::RubberBandStretcher;
    const auto options = static_cast<Stretcher::Options>(
        Stretcher::OptionProcessRealTime |
        Stretcher::OptionEngineFaster |
        Stretcher::OptionPitchHighConsistency |
        Stretcher::OptionChannelsTogether);
    const double safeSpeed = std::clamp(speed, 0.1, 5.0);
    m_stretcher = std::make_unique<Stretcher>(
        std::max(1, sampleRate), static_cast<size_t>(m_channels), options,
        1.0 / safeSpeed, 1.0);
    m_stretcher->setMaxProcessSize(m_maxProcessFrames);

    m_inputChannels.resize(static_cast<size_t>(m_channels));
    m_outputChannels.resize(static_cast<size_t>(m_channels));
    m_inputPointers.resize(static_cast<size_t>(m_channels));
    m_outputPointers.resize(static_cast<size_t>(m_channels));
    for (int channel = 0; channel < m_channels; ++channel)
    {
        m_inputChannels[channel].resize(m_maxProcessFrames);
        m_outputChannels[channel].resize(m_maxProcessFrames);
        m_inputPointers[channel] = m_inputChannels[channel].data();
        m_outputPointers[channel] = m_outputChannels[channel].data();
    }

    // Real-time mode does not compensate its own analysis latency. Prime it
    // with the recommended silence and discard the matching output delay so
    // the first audible sample remains aligned with the video clock.
    m_startDelayRemaining = m_stretcher->getStartDelay();
    size_t paddingRemaining = m_stretcher->getPreferredStartPad();
    while (paddingRemaining > 0)
    {
        const size_t block = std::min(paddingRemaining, m_maxProcessFrames);
        for (auto& samples : m_inputChannels)
            std::fill_n(samples.begin(), block, 0.0f);
        ProcessPlanar(block, false);
        paddingRemaining -= block;
    }
}

PitchPreservingStretcher::~PitchPreservingStretcher() = default;

size_t PitchPreservingStretcher::GetSamplesRequired() const
{
    return m_stretcher ? m_stretcher->getSamplesRequired() : 0;
}

size_t PitchPreservingStretcher::GetAvailableFrameCount() const
{
    if (!m_stretcher)
        return 0;
    const int available = m_stretcher->available();
    if (available <= 0 || static_cast<size_t>(available) <= m_startDelayRemaining)
        return 0;
    return static_cast<size_t>(available) - m_startDelayRemaining;
}

void PitchPreservingStretcher::ProcessPlanar(size_t frames, bool final)
{
    m_stretcher->process(m_inputPointers.data(), frames, final);
}

void PitchPreservingStretcher::ProcessInterleaved(
    const float* input, size_t frames, bool final)
{
    if (frames == 0)
    {
        if (final)
            ProcessPlanar(0, true);
        return;
    }

    size_t offset = 0;
    while (offset < frames)
    {
        const size_t block = std::min(frames - offset, m_maxProcessFrames);
        for (int channel = 0; channel < m_channels; ++channel)
        {
            for (size_t frame = 0; frame < block; ++frame)
            {
                m_inputChannels[channel][frame] =
                    input ? input[(offset + frame) * m_channels + channel] : 0.0f;
            }
        }
        const bool finalBlock = final && offset + block == frames;
        ProcessPlanar(block, finalBlock);
        offset += block;
    }
}

void PitchPreservingStretcher::DiscardStartDelay()
{
    while (m_startDelayRemaining > 0)
    {
        const int available = m_stretcher->available();
        if (available <= 0)
            return;
        const size_t discard = std::min({
            m_startDelayRemaining, static_cast<size_t>(available),
            m_maxProcessFrames});
        const size_t retrieved =
            m_stretcher->retrieve(m_outputPointers.data(), discard);
        if (retrieved == 0)
            return;
        m_startDelayRemaining -= retrieved;
    }
}

size_t PitchPreservingStretcher::RetrieveInterleaved(
    float* output, size_t maxFrames)
{
    if (!m_stretcher || !output || maxFrames == 0)
        return 0;
    DiscardStartDelay();

    const int available = m_stretcher->available();
    if (available <= 0)
        return 0;
    const size_t requested = std::min({
        maxFrames, static_cast<size_t>(available), m_maxProcessFrames});
    const size_t retrieved =
        m_stretcher->retrieve(m_outputPointers.data(), requested);
    for (size_t frame = 0; frame < retrieved; ++frame)
    {
        for (int channel = 0; channel < m_channels; ++channel)
            output[frame * m_channels + channel] =
                m_outputChannels[channel][frame];
    }
    return retrieved;
}
