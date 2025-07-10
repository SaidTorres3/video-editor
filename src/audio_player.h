#pragma once

#include "video_player.h"
#include <vector>
#include <atomic>
#include <thread>

class VideoPlayer;

class AudioPlayer {
public:
    explicit AudioPlayer(VideoPlayer* player);
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
    void AudioThread();
    void Mix(float* output, UINT32 frames);
    bool TracksHaveData() const;

    VideoPlayer* m_player;
    std::thread m_thread;
    std::atomic<bool> m_running;
};
