#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

enum class WaveformCoverage : uint8_t { Missing, Preview, Primary };

struct AudioWaveformTrack
{
    int streamIndex = -1;
    std::vector<float> samples;
    std::vector<uint8_t> probableSpeech;
    // Silence is covered too; missing bins must not connect separate traces.
    std::vector<WaveformCoverage> coverage;
};

inline void MergeAudioWaveforms(std::vector<AudioWaveformTrack>& cache,
                                const std::vector<AudioWaveformTrack>& incoming)
{
    for (const auto& source : incoming)
    {
        auto target = std::find_if(cache.begin(), cache.end(), [&](const auto& track) {
            return track.streamIndex == source.streamIndex;
        });
        if (target == cache.end())
        {
            cache.push_back(source);
            continue;
        }
        for (size_t bin = 0; bin < source.coverage.size(); ++bin)
        {
            if (source.coverage[bin] == WaveformCoverage::Missing ||
                source.coverage[bin] < target->coverage[bin])
                continue;
            target->samples[bin] = source.samples[bin];
            target->probableSpeech[bin] = source.probableSpeech[bin];
            target->coverage[bin] = source.coverage[bin];
        }
    }
}
