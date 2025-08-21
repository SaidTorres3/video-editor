#include "noise_reduction.h"
#include <cmath>

void ApplyNoiseReduction(int16_t* samples, int sampleCount, int channels, double& noiseFloor)
{
    const double smooth = 0.95;            // smoothing factor for noise floor
    const double threshold = 1.5;          // noise threshold multiplier
    const double attenuation = 0.1;        // attenuation factor for noise

    for (int i = 0; i < sampleCount; ++i)
    {
        double magnitude = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            magnitude += std::abs(samples[i * channels + ch]);
        magnitude /= channels;

        if (noiseFloor == 0.0)
            noiseFloor = magnitude;
        else
            noiseFloor = smooth * noiseFloor + (1.0 - smooth) * magnitude;

        if (magnitude < noiseFloor * threshold)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                samples[i * channels + ch] = static_cast<int16_t>(samples[i * channels + ch] * attenuation);
            }
        }
    }
}

