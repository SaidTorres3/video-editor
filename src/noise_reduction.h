#pragma once

#include <cstdint>

// Apply simple noise reduction in-place on audio samples.
// samples: pointer to interleaved int16 samples
// sampleCount: number of frames (per channel)
// channels: number of audio channels
// noiseFloor: running estimate of noise level (updated in-place)
void ApplyNoiseReduction(int16_t* samples, int sampleCount, int channels, double& noiseFloor);

