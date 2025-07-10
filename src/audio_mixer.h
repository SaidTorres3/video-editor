#pragma once
#include "video_player.h"
#include <vector>
#include <memory>

class AudioMixer {
public:
    AudioMixer(int sampleRate, int channels);
    void Mix(std::vector<std::unique_ptr<AudioTrack>>& tracks, float* out, int frames, double startPts);
    void Flush();
private:
    int m_sampleRate;
    int m_channels;
};
