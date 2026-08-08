#include "video_renderer.h"
#include "video_player.h"
#include "video_decoder.h"
#include <algorithm>
#include <cmath>
#include <cwchar>

VideoRenderer::VideoRenderer(VideoPlayer* player) : m_player(player) {}

VideoRenderer::~VideoRenderer() {
    Cleanup();
}

bool VideoRenderer::Initialize() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_player->d2dFactory);
    if (FAILED(hr))
        return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(&m_player->dwriteFactory));
    if (SUCCEEDED(hr))
    {
        hr = m_player->dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            32.0f, L"en-us", &m_player->speedTextFormat);
        if (SUCCEEDED(hr))
        {
            m_player->speedTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_player->speedTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    return true;
}

void VideoRenderer::Cleanup() {
    if (m_player->speedTextFormat)
    {
        m_player->speedTextFormat->Release();
        m_player->speedTextFormat = nullptr;
    }
    if (m_player->dwriteFactory)
    {
        m_player->dwriteFactory->Release();
        m_player->dwriteFactory = nullptr;
    }
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

void VideoRenderer::UpdateDisplay(bool waitForFrame) {
    if (!m_player->d2dRenderTarget || !m_player->frameRGB->data[0])
        return;

    std::unique_lock<std::mutex> lock(m_player->renderMutex, std::defer_lock);
    if (waitForFrame)
    {
        lock.lock();
    }
    else if (!lock.try_lock())
    {
        // Full-resolution conversion can hold this lock long enough to starve
        // the UI timer. Keep WM_PAINT non-blocking; the converter invalidates
        // the video window when it publishes the next frame, so repaint will
        // retry without creating a busy WM_PAINT loop that starves WM_TIMER.
        return;
    }

    const bool usePreview = m_player->displayUsesPlaybackBuffer &&
                            !m_player->playbackRgbBuffer.empty();
    const uint8_t* displayData = usePreview
        ? m_player->playbackRgbBuffer.data()
        : m_player->frameRGB->data[0];
    const int bitmapWidth = usePreview ? m_player->playbackRgbWidth : m_player->frameWidth;
    const int bitmapHeight = usePreview ? m_player->playbackRgbHeight : m_player->frameHeight;
    const int bitmapStride = usePreview ? m_player->playbackRgbStride : m_player->frameRGB->linesize[0];

    if (m_player->d2dBitmap)
    {
        const D2D1_SIZE_U bitmapSize = m_player->d2dBitmap->GetPixelSize();
        if (bitmapSize.width != static_cast<UINT32>(bitmapWidth) ||
            bitmapSize.height != static_cast<UINT32>(bitmapHeight))
        {
            m_player->d2dBitmap->Release();
            m_player->d2dBitmap = nullptr;
        }
    }

    if (!m_player->d2dBitmap)
    {
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        m_player->d2dRenderTarget->CreateBitmap(
            D2D1::SizeU(bitmapWidth, bitmapHeight),
            displayData,
            bitmapStride,
            props,
            &m_player->d2dBitmap);
    }
    else
    {
        D2D1_RECT_U rect = {0, 0, (UINT32)bitmapWidth, (UINT32)bitmapHeight};
        m_player->d2dBitmap->CopyFromMemory(&rect, displayData, bitmapStride);
    }

    m_player->d2dRenderTarget->BeginDraw();
    m_player->d2dRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));
    D2D1_SIZE_F size = m_player->d2dRenderTarget->GetSize();
    int srcW = m_player->frameWidth;
    int srcH = m_player->frameHeight;
    const float sourceScaleX = bitmapWidth / static_cast<float>(m_player->frameWidth);
    const float sourceScaleY = bitmapHeight / static_cast<float>(m_player->frameHeight);
    D2D1_RECT_F srcRect = {};
    if (m_player->hasCrop) {
        srcW = m_player->cropRect.right - m_player->cropRect.left;
        srcH = m_player->cropRect.bottom - m_player->cropRect.top;
        srcRect = D2D1::RectF(
            static_cast<FLOAT>(m_player->cropRect.left) * sourceScaleX,
            static_cast<FLOAT>(m_player->cropRect.top) * sourceScaleY,
            static_cast<FLOAT>(m_player->cropRect.right) * sourceScaleX,
            static_cast<FLOAT>(m_player->cropRect.bottom) * sourceScaleY);
    }
    float targetAspect = size.width / size.height;
    float videoAspect = static_cast<float>(srcW) / srcH;
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
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
        m_player->hasCrop ? &srcRect : nullptr);

    if (m_player->selectingCrop) {
        D2D1_RECT_F sel = D2D1::RectF(
            (FLOAT)std::min(m_player->cropStart.x, m_player->cropCurrent.x),
            (FLOAT)std::min(m_player->cropStart.y, m_player->cropCurrent.y),
            (FLOAT)std::max(m_player->cropStart.x, m_player->cropCurrent.x),
            (FLOAT)std::max(m_player->cropStart.y, m_player->cropCurrent.y));
        ID2D1SolidColorBrush* brush = nullptr;
        m_player->d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Yellow), &brush);
        m_player->d2dRenderTarget->DrawRectangle(sel, brush, 2.0f);
        if (brush) brush->Release();
    }

    if (m_player->IsPlaybackSpeedOverlayVisible() && m_player->speedTextFormat)
    {
        wchar_t label[64];
        const double speed = m_player->GetPlaybackSpeed();
        if (std::fabs(speed - std::round(speed)) < 0.001)
            swprintf_s(label, L"Speed: %.0fx", speed);
        else
            swprintf_s(label, L"Speed: %.1fx", speed);

        const float badgeWidth = std::min(260.0f, std::max(150.0f, drawWidth - 24.0f));
        const float badgeHeight = 58.0f;
        const float badgeLeft = offsetX + (drawWidth - badgeWidth) / 2.0f;
        const float badgeTop = offsetY + 20.0f;
        const D2D1_ROUNDED_RECT badge = D2D1::RoundedRect(
            D2D1::RectF(badgeLeft, badgeTop, badgeLeft + badgeWidth, badgeTop + badgeHeight),
            10.0f, 10.0f);

        ID2D1SolidColorBrush* backgroundBrush = nullptr;
        ID2D1SolidColorBrush* textBrush = nullptr;
        m_player->d2dRenderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.72f), &backgroundBrush);
        m_player->d2dRenderTarget->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::White), &textBrush);
        if (backgroundBrush)
            m_player->d2dRenderTarget->FillRoundedRectangle(badge, backgroundBrush);
        if (textBrush)
            m_player->d2dRenderTarget->DrawTextW(
                label, static_cast<UINT32>(wcslen(label)), m_player->speedTextFormat,
                badge.rect, textBrush);
        if (backgroundBrush) backgroundBrush->Release();
        if (textBrush) textBrush->Release();
    }
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
        UpdateDisplay(false);
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
