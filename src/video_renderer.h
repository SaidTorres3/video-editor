#pragma once

#include "video_player.h"

class VideoPlayer;

class VideoRenderer {
public:
    VideoRenderer(VideoPlayer* player);
    ~VideoRenderer();

    bool Initialize();
    void Cleanup();
    void UpdateDisplay();
    void SetPosition(int x, int y, int width, int height);
    void Render();
    void OnVideoWindowPaint();

public:
    bool CreateRenderTarget();

    VideoPlayer* m_player;
};
