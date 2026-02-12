#pragma once

class VideoPlayer;

class VideoRenderer {
public:
    VideoRenderer(VideoPlayer* player);
    ~VideoRenderer();

    bool Initialize();
    void Cleanup();
    void UpdateDisplay();
    void SetPosition(int x, int y, int width, int height);
    void OnVideoWindowPaint();
    bool CreateRenderTarget();

    VideoPlayer* m_player;
};
