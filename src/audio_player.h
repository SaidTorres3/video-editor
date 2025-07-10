#pragma once

#include "video_player.h"
#include <memory>

class AudioMixer;

class VideoPlayer;

class AudioPlayer {
public:
    AudioPlayer(VideoPlayer* player);
    ~AudioPlayer();

    bool Initialize();
    void Cleanup();
    bool InitializeTracks();
    void CleanupTracks();
    void StartThread();
    void StopThread();
    void ProcessFrame(AVPacket* packet);
    void SetMasterVolume(float volume);

private:
    void AudioThreadFunction();

    VideoPlayer* m_player;
    std::unique_ptr<AudioMixer> m_mixer;
    double m_nextAudioPts;
    bool m_outputFloat;
};
