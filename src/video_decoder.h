#pragma once

#include "video_player.h"

class VideoPlayer;

class VideoDecoder {
public:
    VideoDecoder(VideoPlayer* player);
    ~VideoDecoder();

    bool Initialize();
    void Cleanup();
    bool DecodeNextFrame(bool presentFrame, bool scheduleDisplay = true);

private:
    VideoPlayer* m_player;
};
