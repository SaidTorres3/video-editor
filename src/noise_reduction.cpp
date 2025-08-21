#include "noise_reduction.h"
#include <cmath>

void ApplyNoiseReduction(int16_t* samples, int sampleCount, int channels,
                         double& noiseFloor, NoiseReductionLevel level)
{
    if (level == NoiseReductionLevel::Disabled)
        return;

    double smooth = 0.95;
    double threshold = 1.5;
    double attenuation = 0.1;

    switch (level)
    {
    case NoiseReductionLevel::Low:
        smooth = 0.98;
        threshold = 1.2;
        attenuation = 0.5;
        break;
    case NoiseReductionLevel::Normal:
        smooth = 0.95;
        threshold = 1.5;
        attenuation = 0.2;
        break;
    case NoiseReductionLevel::High:
        smooth = 0.90;
        threshold = 2.0;
        attenuation = 0.05;
        break;
    default:
        break;
    }

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

