#pragma once

#include "video_player.h"
#include <chrono>
#include <mutex>

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
    void StopThread(bool resetDevice = false);
    void ProcessFrame(AVPacket* packet);
    void SetMasterVolume(float volume);

private:
    void AudioThreadFunction();
    void MixAudioTracks(uint8_t* outputBuffer, int frameCount, double startPts);
    bool HasBufferedAudio() const;
    int GetAvailableFrameCount(double startPts) const;

    VideoPlayer* m_player;
    int64_t m_framesWritten;
    float m_masterVolume;
    bool m_comInitializedByUs = false;
    std::mutex m_threadLifecycleMutex;
};
