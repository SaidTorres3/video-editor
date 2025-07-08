#include "video_renderer.h"
#include "video_player.h"
#include "video_decoder.h"

VideoRenderer::VideoRenderer(VideoPlayer* player) : m_player(player) {}

VideoRenderer::~VideoRenderer() {
    Cleanup();
}

bool VideoRenderer::Initialize() {
    return SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_player->d2dFactory));
}

void VideoRenderer::Cleanup() {
    if (m_player->d2dBitmap)
    {
        m_player->d2dBitmap->Release();
        m_player->d2dBitmap = nullptr;
    }
    if (m_player->d2dRenderTarget)
    {
        m_player->d2dRenderTarget->Release();
        m_player->d2dRenderTarget = nullptr;
    }
    if (m_player->d2dFactory)
    {
        m_player->d2dFactory->Release();
        m_player->d2dFactory = nullptr;
    }
}

void VideoRenderer::UpdateDisplay() {
    if (!m_player->d2dRenderTarget || !m_player->frameRGB->data[0])
        return;

    std::lock_guard<std::mutex> lock(m_player->decodeMutex);

    if (!m_player->d2dBitmap)
    {
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        m_player->d2dRenderTarget->CreateBitmap(
            D2D1::SizeU(m_player->frameWidth, m_player->frameHeight),
            m_player->frameRGB->data[0],
            m_player->frameRGB->linesize[0],
            props,
            &m_player->d2dBitmap);
    }
    else
    {
        D2D1_RECT_U rect = {0, 0, (UINT32)m_player->frameWidth, (UINT32)m_player->frameHeight};
        m_player->d2dBitmap->CopyFromMemory(&rect, m_player->frameRGB->data[0], m_player->frameRGB->linesize[0]);
    }

    m_player->d2dRenderTarget->BeginDraw();
    m_player->d2dRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));
    D2D1_SIZE_F size = m_player->d2dRenderTarget->GetSize();
    float targetAspect = size.width / size.height;
    float videoAspect = static_cast<float>(m_player->frameWidth) / m_player->frameHeight;
    float drawWidth = size.width;
    float drawHeight = size.height;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    if (targetAspect > videoAspect)
    {
        drawHeight = size.height;
        drawWidth = drawHeight * videoAspect;
        offsetX = (size.width - drawWidth) / 2.0f;
    }
    else
    {
        drawWidth = size.width;
        drawHeight = drawWidth / videoAspect;
        offsetY = (size.height - drawHeight) / 2.0f;
    }

    m_player->d2dRenderTarget->DrawBitmap(
        m_player->d2dBitmap,
        D2D1::RectF(offsetX, offsetY, offsetX + drawWidth, offsetY + drawHeight),
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    m_player->d2dRenderTarget->EndDraw();
}

void VideoRenderer::SetPosition(int x, int y, int width, int height) {
    if (!m_player->videoWindow)
        return;
    SetWindowPos(m_player->videoWindow, nullptr, x, y, width, height, SWP_NOZORDER);
    if (m_player->d2dRenderTarget)
    {
        m_player->d2dRenderTarget->Resize(D2D1::SizeU(width, height));
    }
    InvalidateRect(m_player->videoWindow, nullptr, TRUE);
    UpdateWindow(m_player->videoWindow);
}

void VideoRenderer::Render() {
    if (m_player->isLoaded && !m_player->isPlaying)
        m_player->m_decoder->DecodeNextFrame(true);
}

void VideoRenderer::OnVideoWindowPaint() {
    PAINTSTRUCT ps;
    BeginPaint(m_player->videoWindow, &ps);
    if (m_player->isLoaded)
        UpdateDisplay();
    else
        FillRect(ps.hdc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
    EndPaint(m_player->videoWindow, &ps);
}

bool VideoRenderer::CreateRenderTarget() {
    if (!m_player->d2dFactory || !m_player->videoWindow)
        return false;
    RECT rc;
    GetClientRect(m_player->videoWindow, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    HRESULT hr = m_player->d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(m_player->videoWindow, size),
        &m_player->d2dRenderTarget);
    return SUCCEEDED(hr);
}