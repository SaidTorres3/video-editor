#include "audio_mixer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

AudioMixer::AudioMixer(int sampleRate, int channels)
    : m_sampleRate(sampleRate), m_channels(channels) {}

void AudioMixer::Flush() {}

void AudioMixer::Mix(std::vector<std::unique_ptr<AudioTrack>>& tracks, float* out, int frames, double startPts)
{
    const double sampleDur = 1.0 / m_sampleRate;
    std::fill(out, out + frames * m_channels, 0.f);

    for (int i = 0; i < frames; ++i) {
        double pts = startPts + i * sampleDur;
        for (auto& tptr : tracks) {
            AudioTrack* tr = tptr.get();
            if (tr->isMuted)
                continue;

            while (tr->buffer.size() >= (size_t)m_channels &&
                   tr->nextPts + sampleDur < pts - 0.02) {
                for (int c = 0; c < m_channels; ++c)
                    tr->buffer.pop_front();
                tr->nextPts += sampleDur;
            }

            if (tr->buffer.size() >= (size_t)m_channels &&
                std::abs(tr->nextPts - pts) <= 0.02) {
                for (int c = 0; c < m_channels; ++c) {
                    float s = tr->buffer.front();
                    tr->buffer.pop_front();
                    out[i * m_channels + c] += s * tr->volume;
                }
                tr->nextPts += sampleDur;
            }
        }
    }

    for (int i = 0; i < frames * m_channels; ++i) {
        if (out[i] > 1.f)
            out[i] = 1.f;
        else if (out[i] < -1.f)
            out[i] = -1.f;
    }
}
