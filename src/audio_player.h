#pragma once

#include "video_player.h"
#include <chrono>

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
    void MixAudioTracks(uint8_t* outputBuffer, int frameCount, double startPts);
    bool HasBufferedAudio() const;

    VideoPlayer* m_player;
    int64_t m_framesWritten;
};
