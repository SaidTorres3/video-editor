#pragma once

#include "video_player.h"
#include "clip_segment.h"
#include <chrono>
#include <vector>

class VideoPlayer;
enum class EncoderSelection : int;

class VideoCutter {
public:
    VideoCutter(VideoPlayer* player);
    ~VideoCutter();

    bool CutVideo(const std::wstring& outputFilename, double startTime, double endTime,
                  bool mergeAudio, bool convertH264, EncoderSelection encoder, const std::wstring& qualityPreset,
                  int maxBitrate, HWND progressBar, std::atomic<bool>* cancelFlag);
    bool CutVideo(const std::wstring& outputFilename, const std::vector<ClipSegment>& segments,
                  bool mergeAudio, bool convertH264, EncoderSelection encoder, const std::wstring& qualityPreset,
                  int maxBitrate, HWND progressBar, std::atomic<bool>* cancelFlag);

private:
    VideoPlayer* m_player;
    int m_lastDisplayedPercent = -1;
    std::chrono::high_resolution_clock::time_point m_lastUpdateTime;
    
    void ResetProgressTracking();
};
