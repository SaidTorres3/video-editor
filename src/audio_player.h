#pragma once

#include "video_player.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

class VideoPlayer;
class PitchPreservingStretcher;

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
    void ResetPitchPreservingTimeStretch(double startPts);
    void MixPitchPreservedAudio(uint8_t* outputBuffer, int frameCount);
    void MixSourceFramesForTimeStretch(int frameCount);
    bool HasBufferedAudio() const;
    int GetAvailableSourceFrameCount(double startPts) const;
    int GetAvailableFrameCount(double startPts) const;

    VideoPlayer* m_player;
    int64_t m_framesWritten;
    float m_masterVolume;
    bool m_comInitializedByUs = false;
    std::mutex m_threadLifecycleMutex;
    std::unique_ptr<PitchPreservingStretcher> m_timeStretcher;
    std::deque<float> m_stretchedOutput;
    std::vector<float> m_stretchInputBuffer;
    std::vector<float> m_stretchRetrieveBuffer;
    double m_nextStretchSourcePts = 0.0;
#ifdef VIDEO_EDITOR_TESTING
    std::atomic<uint64_t> m_submittedFrameCount{0};
    std::atomic<uint64_t> m_clientStartCount{0};
    std::atomic<uint64_t> m_clientStartFailureCount{0};
#endif
};
