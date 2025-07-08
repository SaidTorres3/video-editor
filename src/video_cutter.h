#pragma once

#include "video_player.h"

class VideoPlayer;

class VideoCutter {
public:
    VideoCutter(VideoPlayer* player);
    ~VideoCutter();

    bool CutVideo(const std::wstring& outputFilename, double startTime, double endTime,
                  bool mergeAudio, bool convertH264, bool useNvenc,
                  int maxBitrate, HWND progressBar, std::atomic<bool>* cancelFlag);

private:
    VideoPlayer* m_player;
};
