// video_renderer.cpp - Minimal no-op implementation for ImGui migration
// Video frame display is now handled by imgui_ui.cpp via DX11 textures
#include "video_renderer.h"
#include "video_player.h"

VideoRenderer::VideoRenderer(VideoPlayer* player) : m_player(player) {}
VideoRenderer::~VideoRenderer() { Cleanup(); }

bool VideoRenderer::Initialize() { return true; }
void VideoRenderer::Cleanup() {}
void VideoRenderer::UpdateDisplay() {}
void VideoRenderer::SetPosition(int, int, int, int) {}
void VideoRenderer::OnVideoWindowPaint() {}
bool VideoRenderer::CreateRenderTarget() { return true; }