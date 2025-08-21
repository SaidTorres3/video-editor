#include "noise_reduction.h"
#include <cmath>
#include <complex>
#include <vector>

namespace {
constexpr int kFftSize = 256;
constexpr double kPi = 3.14159265358979323846;

struct WindowInit {
    std::vector<double> win;
    WindowInit() : win(kFftSize) {
        for (int i = 0; i < kFftSize; ++i)
            win[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (kFftSize - 1));
    }
};

const WindowInit kWindow;
}

void ApplyNoiseReduction(int16_t* samples, int sampleCount, int channels,
                         std::vector<double>& noiseProfile, NoiseReductionLevel level)
{
    if (level == NoiseReductionLevel::Disabled)
        return;

    if (noiseProfile.size() < kFftSize / 2 + 1)
        noiseProfile.assign(kFftSize / 2 + 1, 0.0);

    double suppression = 0.0;
    double update = 0.95;
    switch (level)
    {
    case NoiseReductionLevel::Low:
        suppression = 0.3;
        update = 0.98;
        break;
    case NoiseReductionLevel::Normal:
        suppression = 0.5;
        update = 0.95;
        break;
    case NoiseReductionLevel::High:
        suppression = 0.8;
        update = 0.90;
        break;
    default:
        break;
    }

    std::vector<std::complex<double>> spectrum(kFftSize);
    std::vector<double> magnitudes(kFftSize / 2 + 1);

    for (int pos = 0; pos + kFftSize <= sampleCount; pos += kFftSize)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            for (int n = 0; n < kFftSize; ++n)
            {
                double s = samples[(pos + n) * channels + ch];
                spectrum[n] = s * kWindow.win[n];
            }

            // DFT
            for (int k = 0; k < kFftSize; ++k)
            {
                std::complex<double> sum(0.0, 0.0);
                for (int n = 0; n < kFftSize; ++n)
                {
                    double angle = -2.0 * kPi * k * n / kFftSize;
                    sum += spectrum[n] * std::complex<double>(std::cos(angle), std::sin(angle));
                }
                spectrum[k] = sum;
            }

            for (int k = 0; k <= kFftSize / 2; ++k)
            {
                magnitudes[k] = std::abs(spectrum[k]);
                noiseProfile[k] = update * noiseProfile[k] + (1.0 - update) * magnitudes[k];
                double cleanMag = magnitudes[k] - noiseProfile[k];
                if (cleanMag < 0.0)
                    cleanMag = 0.0;
                double targetMag = noiseProfile[k] + cleanMag * (1.0 - suppression);
                double gain = targetMag / (magnitudes[k] + 1e-9);
                spectrum[k] *= gain;
                if (k > 0 && k < kFftSize / 2)
                    spectrum[kFftSize - k] = std::conj(spectrum[k]);
            }

            // Inverse DFT
            for (int n = 0; n < kFftSize; ++n)
            {
                std::complex<double> sum(0.0, 0.0);
                for (int k = 0; k < kFftSize; ++k)
                {
                    double angle = 2.0 * kPi * k * n / kFftSize;
                    sum += spectrum[k] * std::complex<double>(std::cos(angle), std::sin(angle));
                }
                double val = sum.real() / kFftSize;
                val /= kWindow.win[n];
                if (val > 32767.0) val = 32767.0;
                if (val < -32768.0) val = -32768.0;
                samples[(pos + n) * channels + ch] = static_cast<int16_t>(val);
            }
        }
    }
}
