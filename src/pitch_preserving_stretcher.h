#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace RubberBand
{
class RubberBandStretcher;
}

// Small interleaved-audio adapter around Rubber Band's planar real-time API.
// timeRatio is derived from playback speed; pitch scale always remains 1.0.
class PitchPreservingStretcher
{
public:
    PitchPreservingStretcher(int sampleRate, int channels, double speed);
    ~PitchPreservingStretcher();

    PitchPreservingStretcher(const PitchPreservingStretcher&) = delete;
    PitchPreservingStretcher& operator=(const PitchPreservingStretcher&) = delete;

    size_t GetSamplesRequired() const;
    size_t GetAvailableFrameCount() const;
    void ProcessInterleaved(const float* input, size_t frames, bool final);
    size_t RetrieveInterleaved(float* output, size_t maxFrames);

private:
    void ProcessPlanar(size_t frames, bool final);
    void DiscardStartDelay();

    int m_channels;
    size_t m_maxProcessFrames;
    size_t m_startDelayRemaining;
    std::unique_ptr<RubberBand::RubberBandStretcher> m_stretcher;
    std::vector<std::vector<float>> m_inputChannels;
    std::vector<std::vector<float>> m_outputChannels;
    std::vector<const float*> m_inputPointers;
    std::vector<float*> m_outputPointers;
};
