#pragma once

#include "video_player.h"

class VideoPlayer;

class VideoDecoder {
public:
    VideoDecoder(VideoPlayer* player);
    ~VideoDecoder();

    bool Initialize();
    void Cleanup();
    bool DecodeNextFrame(bool presentFrame, bool scheduleDisplay = true, bool generateImage = true);
    bool DecodeNextBufferedFrame(AVFrame* outputFrame, double& pts, int64_t& frameNumber);

private:
    VideoPlayer* m_player;
};
