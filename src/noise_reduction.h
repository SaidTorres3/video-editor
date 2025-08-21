#pragma once

#include <cstdint>
#include <vector>

// Levels of noise reduction intensity
enum class NoiseReductionLevel {
    Disabled = 0,
    Low,
    Normal,
    High
};

// Apply spectral noise reduction in-place on audio samples.
// samples: pointer to interleaved int16 samples
// sampleCount: number of frames (per channel)
// channels: number of audio channels
// noiseProfile: running estimate of noise spectrum (updated in-place)
// level: reduction intensity
void ApplyNoiseReduction(int16_t* samples, int sampleCount, int channels,
                         std::vector<double>& noiseProfile, NoiseReductionLevel level);

