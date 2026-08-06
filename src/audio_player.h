#pragma once

#include "video_player.h"
#include <atomic>
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
#ifdef VIDEO_EDITOR_TESTING
    uint64_t GetSubmittedFrameCountForTesting() const {
        return m_submittedFrameCount.load(std::memory_order_acquire);
    }
    uint64_t GetClientStartCountForTesting() const {
        return m_clientStartCount.load(std::memory_order_acquire);
    }
    uint64_t GetClientStartFailureCountForTesting() const {
        return m_clientStartFailureCount.load(std::memory_order_acquire);
    }
#endif

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
#ifdef VIDEO_EDITOR_TESTING
    std::atomic<uint64_t> m_submittedFrameCount{0};
    std::atomic<uint64_t> m_clientStartCount{0};
    std::atomic<uint64_t> m_clientStartFailureCount{0};
#endif
};
