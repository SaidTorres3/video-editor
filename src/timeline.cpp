#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "timeline.h"
#include "video_player.h"
#include "ui_updates.h"
#include "options_window.h"
#include "editing.h"
#include <windowsx.h>
#include <cmath>
#include <climits>
#include <limits>

// Forward declarations
void UpdateControls();
void UpdateCutInfoLabel(HWND hwnd);
void UpdateCutTimeEdits();

// Global variables
extern VideoPlayer *g_videoPlayer;
extern double g_cutStartTime, g_cutEndTime;
extern bool g_isTimelineDragging;
extern bool g_wasPlayingBeforeDrag;
extern bool g_resumePlayAfterSeek;
enum class DragMode { None, Cursor, StartMarker, EndMarker, Keyframe };
extern DragMode g_timelineDragMode;
extern double g_draggedKeyframeTime;
double g_previewSeekTime = -1.0; // For immediate timeline feedback

// Hover tooltip tracking
static int g_timelineHoverX = -1;
static bool g_timelineMouseTracking = false;

// Timecode tooltip popup window
static HWND g_timecodeTooltipWnd = nullptr;
static wchar_t g_timecodeTooltipText[32] = {};
static const wchar_t* TIMECODE_TOOLTIP_CLASS = L"TimelineTimecodeTooltip";

// --- Thumbnail frame preview globals ---
static double g_timelineHoverTime = -1.0; // Quantized hover time for thumbnail lookup

// Thumbnail popup dimensions
static const int THUMB_W          = 160;
static const int THUMB_MAX_H      = 90;
static const int THUMB_TIMECODE_H = 22;

struct ThumbnailData
{
    double               time   = -1.0;
    int                  width  = 0;
    int                  height = 0;
    std::vector<uint8_t> pixels; // BGRA, width*height*4 bytes
};

static std::mutex                 g_thumbCacheMutex;
static std::vector<ThumbnailData> g_thumbCache;
static const int                  THUMB_CACHE_SIZE = 250;

static std::atomic<double>        g_thumbRequestTime{-1.0};
static std::atomic<bool>          g_thumbThreadExit{false};
static HANDLE                     g_thumbRequestEvent = nullptr;
static std::thread                g_thumbThread;

// Pre-cache queue: populated when a video is loaded.
static std::mutex              g_preCacheMutex;
static std::vector<double>     g_preCacheTimes;

// Public: call after a new video loads to queue up eager pre-cache work.
void TriggerThumbnailPreCache(double duration)
{
    if (duration <= 0.0) return;
    // Compute an interval so we generate at most 48 thumbnails, min 0.5 s apart.
    const int MAX_THUMBS = 200;
    double interval = duration / MAX_THUMBS;
    if (interval < 0.25) interval = 0.25;

    {
        std::lock_guard<std::mutex> lck(g_preCacheMutex);
        g_preCacheTimes.clear();
        for (double t = 0.0; t < duration; t += interval)
            g_preCacheTimes.push_back(t);
        // Also clear old cache since it belongs to a different video.
        std::lock_guard<std::mutex> lck2(g_thumbCacheMutex);
        g_thumbCache.clear();
    }
    if (g_thumbRequestEvent)
        SetEvent(g_thumbRequestEvent);
}

static void ThumbnailThreadFunc()
{
    while (!g_thumbThreadExit.load())
    {
        if (WaitForSingleObject(g_thumbRequestEvent, 200) == WAIT_OBJECT_0)
            ResetEvent(g_thumbRequestEvent);
        if (g_thumbThreadExit.load()) break;

        // Determine what to decode: priority = hover request, fallback = next pre-cache slot.
        double reqTime = g_thumbRequestTime.exchange(-1.0);
        bool isPreCache = false;
        if (reqTime < 0.0) {
            std::lock_guard<std::mutex> lck(g_preCacheMutex);
            if (!g_preCacheTimes.empty()) {
                reqTime = g_preCacheTimes.front();
                g_preCacheTimes.erase(g_preCacheTimes.begin());
                isPreCache = true;
            }
        }
        if (reqTime < 0.0) continue;

        // Skip if already in cache
        {
            std::lock_guard<std::mutex> lck(g_thumbCacheMutex);
            bool found = false;
            for (const auto& e : g_thumbCache)
                if (std::fabs(e.time - reqTime) < 0.3) { found = true; break; }
            if (found) {
                // Signal again to drain pre-cache queue
                if (isPreCache && g_thumbRequestEvent) SetEvent(g_thumbRequestEvent);
                continue;
            }
        }

        if (!g_videoPlayer || !g_videoPlayer->IsLoaded()) continue;

        int fw = g_videoPlayer->frameWidth;
        int fh = g_videoPlayer->frameHeight;
        int tw = THUMB_W, th = THUMB_MAX_H;
        if (fw > 0 && fh > 0)
        {
            th = (int)((double)THUMB_W * fh / fw);
            if (th > THUMB_MAX_H) { th = THUMB_MAX_H; tw = (int)((double)THUMB_MAX_H * fw / fh); }
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
        }

        ThumbnailData td;
        td.time   = reqTime;
        td.width  = tw;
        td.height = th;
        // Use fast (persistent decoder) path; fall back to slow path if not ready yet.
        bool ok = g_videoPlayer->GetThumbnailPixelsFast(reqTime, tw, th, td.pixels);
        if (!ok)
            ok = g_videoPlayer->GetThumbnailPixels(reqTime, tw, th, td.pixels);
        if (ok)
        {
            {
                std::lock_guard<std::mutex> lck(g_thumbCacheMutex);
                if (g_thumbCache.size() >= THUMB_CACHE_SIZE)
                    g_thumbCache.erase(g_thumbCache.begin());
                g_thumbCache.push_back(std::move(td));
            }
            // Always repaint: pre-cached frames appear immediately on hover
            if (g_timecodeTooltipWnd)
                InvalidateRect(g_timecodeTooltipWnd, nullptr, FALSE);
        }
        // Keep draining pre-cache queue
        if (isPreCache && g_thumbRequestEvent)
            SetEvent(g_thumbRequestEvent);
    }
}



static LRESULT CALLBACK TimecodeTooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // Dark background
        HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        if (!g_showVideoPreviewOnHover)
        {
            // Timecode-only mode: centre text in the whole window
            HFONT font = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                    DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
            HGDIOBJ oldFont = SelectObject(hdc, font);
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextW(hdc, g_timecodeTooltipText, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldFont);
            DeleteObject(font);
            EndPaint(hwnd, &ps);
            return 0;
        }

        // Copy the nearest cached thumbnail (any distance) — always show something
        std::vector<uint8_t> pixels;
        int thumbW = 0, thumbH = 0;
        {
            std::lock_guard<std::mutex> lck(g_thumbCacheMutex);
            double bestDist = 1e300;
            for (const auto& e : g_thumbCache)
            {
                if (e.pixels.empty()) continue;
                double d = std::fabs(e.time - g_timelineHoverTime);
                if (d < bestDist)
                {
                    bestDist = d;
                    pixels  = e.pixels;
                    thumbW  = e.width;
                    thumbH  = e.height;
                }
            }
        }

        const int tcH      = THUMB_TIMECODE_H;
        int       thumbAreaH = rc.bottom - tcH;

        if (!pixels.empty() && thumbW > 0 && thumbH > 0)
        {
            BITMAPINFO bmi          = {};
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = thumbW;
            bmi.bmiHeader.biHeight      = -thumbH; // top-down DIB
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            int drawX = (rc.right  - thumbW) / 2;
            int drawY = (thumbAreaH - thumbH) / 2;
            SetDIBitsToDevice(hdc, drawX, drawY, thumbW, thumbH,
                              0, 0, 0, thumbH, pixels.data(), &bmi, DIB_RGB_COLORS);
        }

        // Separator line
        HBRUSH sep     = CreateSolidBrush(RGB(60, 60, 60));
        RECT   sepRect = { 0, thumbAreaH, rc.right, thumbAreaH + 1 };
        FillRect(hdc, &sepRect, sep);
        DeleteObject(sep);

        // Timecode text in bottom strip
        RECT tcRect = { 0, thumbAreaH, rc.right, rc.bottom };
        HFONT font = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
        HGDIOBJ oldFont = SelectObject(hdc, font);
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);
        DrawTextW(hdc, g_timecodeTooltipText, -1, &tcRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(font);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Timeline zoom variables
double g_timelineZoomLevel = 1.0;  // 1.0 = full video, 2.0 = 2x zoom, etc.
double g_timelineScrollOffset = 0.0;  // Horizontal scroll offset in seconds

// Timeline scroll arrow state
enum class ScrollArrowState { None, LeftArrow, RightArrow };
ScrollArrowState g_scrollArrowPressed = ScrollArrowState::None;
const int SCROLL_ARROW_TIMER_ID = 1001;
const int SCROLL_ARROW_TIMER_INTERVAL = 50;  // milliseconds

// Context-menu keyframe move state (move follows cursor until next click).
bool g_isContextKeyframeMoveMode = false;
double g_contextMovingKeyframeTime = -1.0;

// Helper function to convert pixel X coordinate to time, accounting for zoom and scroll
inline double PixelToTime(int x, RECT &rc, double duration)
{
    if (rc.right <= 0 || duration <= 0)
        return 0.0;
    double ratio = x / (double)rc.right;
    double timeRange = duration / g_timelineZoomLevel;
    return g_timelineScrollOffset + (ratio * timeRange);
}

// Helper function to convert time to pixel X coordinate, accounting for zoom and scroll
inline int TimeToPixel(double time, RECT &rc, double duration)
{
    if (duration <= 0 || g_timelineZoomLevel <= 0)
        return 0;
    if (time < g_timelineScrollOffset)
        return -1000;
    double relativeTime = time - g_timelineScrollOffset;
    double timeRange = duration / g_timelineZoomLevel;
    if (relativeTime > timeRange)
        return rc.right + 1000;
    double ratio = relativeTime / timeRange;
    return (int)(ratio * rc.right);
}

inline double ClampTimelineTimeFromMouseX(int x, RECT& rc, double duration)
{
    if (rc.right <= 0 || duration <= 0.0)
        return 0.0;

    if (x < 0)
        x = 0;
    else if (x > rc.right)
        x = rc.right;

    double seekTime = PixelToTime(x, rc, duration);
    if (seekTime < 0.0)
        seekTime = 0.0;
    if (seekTime >= duration)
    {
        // Keep the keyframe on the last displayable frame instead of placing it past EOF.
        double frameTime = g_videoPlayer->frameRate > 0 ? (1.0 / g_videoPlayer->frameRate) : 0.033;
        seekTime = duration - frameTime;
        if (seekTime < 0.0)
            seekTime = 0.0;
    }

    return seekTime;
}

LRESULT CALLBACK TimelineProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = TimecodeTooltipWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = TIMECODE_TOOLTIP_CLASS;
        RegisterClassExW(&wc); // Ignore error if already registered
        g_timecodeTooltipWnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            TIMECODE_TOOLTIP_CLASS, L"",
            WS_POPUP | WS_BORDER,
            0, 0, THUMB_W, THUMB_MAX_H + THUMB_TIMECODE_H,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        // Start background thumbnail decode thread
        g_thumbThreadExit.store(false);
        g_thumbRequestTime.store(-1.0);
        g_thumbRequestEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_thumbThread = std::thread(ThumbnailThreadFunc);
        return 0;
    }
    case WM_DESTROY:
        // Stop thumbnail decode thread before destroying the tooltip window
        if (g_thumbRequestEvent)
        {
            g_thumbThreadExit.store(true);
            SetEvent(g_thumbRequestEvent);
        }
        if (g_thumbThread.joinable())
            g_thumbThread.join();
        {
            std::lock_guard<std::mutex> lck(g_thumbCacheMutex);
            g_thumbCache.clear();
        }
        if (g_thumbRequestEvent)
        {
            CloseHandle(g_thumbRequestEvent);
            g_thumbRequestEvent = nullptr;
        }
        if (g_timecodeTooltipWnd)
        {
            DestroyWindow(g_timecodeTooltipWnd);
            g_timecodeTooltipWnd = nullptr;
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            if (g_isContextKeyframeMoveMode)
            {
                RECT rc; GetClientRect(hwnd, &rc);
                double dur = g_videoPlayer->GetDuration();
                if (dur > 0.0 && g_contextMovingKeyframeTime >= 0.0)
                {
                    int x = GET_X_LPARAM(lParam);
                    double targetTime = ClampTimelineTimeFromMouseX(x, rc, dur);
                    g_videoPlayer->MoveCropKeyframe(g_contextMovingKeyframeTime, targetTime);
                }

                g_isContextKeyframeMoveMode = false;
                g_contextMovingKeyframeTime = -1.0;
                if (GetCapture() == hwnd)
                    ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateControls();
                UpdateTimeline();
                return 0;
            }

            SetFocus(hwnd);
            RECT rc; GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            
            // Check if clicking on scroll arrows (only show when zoomed in)
            const int ARROW_WIDTH = 20;
            if (g_timelineZoomLevel > 1.0)
            {
                // Left arrow click
                if (x < ARROW_WIDTH && g_timelineScrollOffset > 0)
                {
                    g_scrollArrowPressed = ScrollArrowState::LeftArrow;
                    SetCapture(hwnd);
                    SetTimer(hwnd, SCROLL_ARROW_TIMER_ID, SCROLL_ARROW_TIMER_INTERVAL, NULL);
                    double dur = g_videoPlayer->GetDuration();
                    double timeRange = dur / g_timelineZoomLevel;
                    g_timelineScrollOffset -= timeRange * 0.1;  // Scroll left by 10%
                    if (g_timelineScrollOffset < 0) g_timelineScrollOffset = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateControls();
                    return 0;
                }
                // Right arrow click
                if (x > rc.right - ARROW_WIDTH)
                {
                    g_scrollArrowPressed = ScrollArrowState::RightArrow;
                    SetCapture(hwnd);
                    SetTimer(hwnd, SCROLL_ARROW_TIMER_ID, SCROLL_ARROW_TIMER_INTERVAL, NULL);
                    double dur = g_videoPlayer->GetDuration();
                    double timeRange = dur / g_timelineZoomLevel;
                    double maxOffset = dur - timeRange;
                    g_timelineScrollOffset += timeRange * 0.1;  // Scroll right by 10%
                    if (g_timelineScrollOffset > maxOffset) g_timelineScrollOffset = maxOffset;
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateControls();
                    return 0;
                }
            }
            
            if (x < 0) x = 0; if (x > rc.right) x = rc.right;
            double dur = g_videoPlayer->GetDuration();
            double seekTime = PixelToTime(x, rc, dur);

            // Check if clicking on a keyframe to drag it
            if (dur > 0.0 && rc.right > 0)
            {
                auto keys = g_videoPlayer->GetCropKeyframes();
                for (const auto& key : keys)
                {
                    if (key.time < 0.0 || key.time > dur)
                        continue;
                    int px = TimeToPixel(key.time, rc, dur);
                    // If clicking within 8 pixels of a keyframe marker, start dragging it
                    if (std::abs(px - x) <= 8)
                    {
                        g_timelineDragMode = DragMode::Keyframe;
                        g_draggedKeyframeTime = key.time;
                        g_isTimelineDragging = true;
                        SetCapture(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                        UpdateControls();
                        return 0;
                    }
                }
            }

            // Disable moving start/end markers via mouse click/drag to prevent
            // accidental adjustments. Treat clicks near markers the same as a
            // regular cursor click so users must use the numeric inputs to
            // adjust `g_cutStartTime` and `g_cutEndTime`.
            if (g_videoPlayer->IsClipPreviewActive())
            {
                // Clamp to just before the clip end so we don't overshoot
                double clampedSeek = seekTime;
                if (g_cutEndTime >= 0 && clampedSeek >= g_cutEndTime)
                {
                    double frameTime = g_videoPlayer->frameRate > 0 ? (1.0 / g_videoPlayer->frameRate) : 0.033;
                    clampedSeek = g_cutEndTime - frameTime;
                    if (clampedSeek < 0.0) clampedSeek = 0.0;
                }
                g_videoPlayer->PlayClip(clampedSeek, g_cutEndTime);
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateControls();
                return 0;
            }
            g_timelineDragMode = DragMode::Cursor;
            g_wasPlayingBeforeDrag = g_videoPlayer->IsPlaying();
            
            // Immediate UI update
            g_previewSeekTime = seekTime;
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateWindow(hwnd); // Force restart of paint cycle to draw line immediately
            
            if (!g_wasPlayingBeforeDrag)
                g_videoPlayer->SeekToTime(seekTime, INT_MAX, false, false);
            else
                g_videoPlayer->SeekWhilePlaying(seekTime, false);
            // Keep g_previewSeekTime pinned to the target; UpdateTimeline() will
            // clear it automatically once actual currentPts catches up.

            g_isTimelineDragging = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        // Update hover position for tooltip
        g_timelineHoverX = GET_X_LPARAM(lParam);
        if (!g_timelineMouseTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_timelineMouseTracking = true;
        }
        // Show timecode tooltip popup above the timeline (always; size depends on preview setting)
        if (g_timecodeTooltipWnd && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            RECT rc; GetClientRect(hwnd, &rc);
            double dur = g_videoPlayer->GetDuration();
            if (dur > 0.0)
            {
                double hoverTime = PixelToTime(g_timelineHoverX, rc, dur);
                if (hoverTime >= 0.0 && hoverTime < dur)
                {
                    int totalSecs = (int)hoverTime;
                    int hours = totalSecs / 3600;
                    int mins = (totalSecs % 3600) / 60;
                    int secs = totalSecs % 60;
                    if (hours > 0)
                        swprintf(g_timecodeTooltipText, 32, L"%d:%02d:%02d", hours, mins, secs);
                    else
                        swprintf(g_timecodeTooltipText, 32, L"%02d:%02d", mins, secs);

                    int tooltipW, tooltipH;
                    if (g_showVideoPreviewOnHover)
                    {
                        // Size to thumbnail + timecode strip
                        int ttW = THUMB_W, ttH = THUMB_MAX_H;
                        int fw = g_videoPlayer->frameWidth;
                        int fh = g_videoPlayer->frameHeight;
                        if (fw > 0 && fh > 0)
                        {
                            ttH = (int)((double)THUMB_W * fh / fw);
                            if (ttH > THUMB_MAX_H) { ttH = THUMB_MAX_H; ttW = (int)((double)THUMB_MAX_H * fw / fh); }
                            if (ttW < 60) ttW = 60;
                            if (ttH < 20) ttH = 20;
                        }
                        tooltipW = ttW;
                        tooltipH = ttH + THUMB_TIMECODE_H;
                    }
                    else
                    {
                        // Timecode-only: measure text and add small padding
                        HDC hdc = GetDC(g_timecodeTooltipWnd);
                        HFONT font = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                               DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
                        HGDIOBJ oldFont = SelectObject(hdc, font);
                        SIZE sz = {};
                        GetTextExtentPoint32W(hdc, g_timecodeTooltipText,
                                              (int)wcslen(g_timecodeTooltipText), &sz);
                        SelectObject(hdc, oldFont);
                        DeleteObject(font);
                        ReleaseDC(g_timecodeTooltipWnd, hdc);
                        const int PAD = 8;
                        tooltipW = sz.cx + PAD * 2;
                        tooltipH = sz.cy + PAD;
                        if (tooltipW < 50) tooltipW = 50;
                    }

                    POINT pt = { g_timelineHoverX, 0 };
                    ClientToScreen(hwnd, &pt);
                    int sx = pt.x - tooltipW / 2;
                    int sy = pt.y - tooltipH - 4;
                    SetWindowPos(g_timecodeTooltipWnd, HWND_TOPMOST, sx, sy, tooltipW, tooltipH,
                                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
                    InvalidateRect(g_timecodeTooltipWnd, nullptr, FALSE);

                    if (g_showVideoPreviewOnHover)
                    {
                        // Request thumbnail decode for the current 0.5 s slot
                        double slotTime = std::floor(hoverTime * 2.0) / 2.0;
                        g_timelineHoverTime = slotTime;
                        bool needDecode = true;
                        {
                            std::lock_guard<std::mutex> lck(g_thumbCacheMutex);
                            for (const auto& e : g_thumbCache)
                                if (std::fabs(e.time - slotTime) < 0.3) { needDecode = false; break; }
                        }
                        if (needDecode && g_thumbRequestEvent)
                        {
                            g_thumbRequestTime.store(slotTime);
                            SetEvent(g_thumbRequestEvent);
                        }
                    }
                }
            }
        }
        if (g_isContextKeyframeMoveMode && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            RECT rc; GetClientRect(hwnd, &rc);
            double dur = g_videoPlayer->GetDuration();
            if (dur > 0.0 && g_contextMovingKeyframeTime >= 0.0)
            {
                int x = GET_X_LPARAM(lParam);
                double targetTime = ClampTimelineTimeFromMouseX(x, rc, dur);
                if (std::fabs(targetTime - g_contextMovingKeyframeTime) >= 0.001 &&
                    g_videoPlayer->MoveCropKeyframe(g_contextMovingKeyframeTime, targetTime))
                {
                    double oldTime = g_contextMovingKeyframeTime;
                    g_contextMovingKeyframeTime = targetTime;
                    if (!g_videoPlayer->IsPlaying())
                    {
                        double curTime = g_videoPlayer->GetCurrentTime();
                        if ((oldTime <= curTime && targetTime >= curTime) || 
                            (oldTime >= curTime && targetTime <= curTime))
                        {
                            if (g_videoPlayer->UpdateCropForTime(curTime))
                                g_videoPlayer->ForceRedraw();
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateControls();
                }
            }
            return 0;
        }

        if (g_isTimelineDragging && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            RECT rc; GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            if (x < 0) x = 0; if (x > rc.right) x = rc.right;
            double dur = g_videoPlayer->GetDuration();
            double seekTime = PixelToTime(x, rc, dur);
            
            // Clamp seekTime to valid range [0, duration)
            if (seekTime < 0.0) seekTime = 0.0;
            if (dur > 0.0 && seekTime >= dur) {
                // Clamp to just before the end (last frame)
                double frameTime = g_videoPlayer->frameRate > 0 ? (1.0 / g_videoPlayer->frameRate) : 0.033;
                seekTime = dur - frameTime;
                if (seekTime < 0.0) seekTime = 0.0;
            }

            if (g_timelineDragMode == DragMode::Cursor)
            {
                // Immediate UI update
                g_previewSeekTime = seekTime;
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindow(hwnd);
                
                if (g_wasPlayingBeforeDrag)
                    g_videoPlayer->SeekWhilePlaying(seekTime, false);
                else
                    g_videoPlayer->SeekToTime(seekTime, 0);
                // Keep g_previewSeekTime pinned; UpdateTimeline() auto-clears it.
            }
            else if (g_timelineDragMode == DragMode::Keyframe)
            {
                // Move the dragged keyframe to the new time
                if (g_draggedKeyframeTime >= 0.0 && dur > 0.0)
                {
                    double oldTime = g_draggedKeyframeTime;
                    g_videoPlayer->MoveCropKeyframe(g_draggedKeyframeTime, seekTime);
                    g_draggedKeyframeTime = seekTime;  // Update the tracked time
                    
                    if (!g_videoPlayer->IsPlaying())
                    {
                        double curTime = g_videoPlayer->GetCurrentTime();
                        if ((oldTime <= curTime && seekTime >= curTime) || 
                            (oldTime >= curTime && seekTime <= curTime))
                        {
                            if (g_videoPlayer->UpdateCropForTime(curTime))
                                g_videoPlayer->ForceRedraw();
                        }
                    }
                }
            }
            else if (g_timelineDragMode == DragMode::StartMarker)
            {
                if (g_cutEndTime >= 0 && seekTime >= g_cutEndTime)
                    seekTime = g_cutEndTime - 0.01;
                if (seekTime < 0) seekTime = 0;
                g_cutStartTime = seekTime;
                UpdateCutInfoLabel(GetParent(hwnd));
            }
            else if (g_timelineDragMode == DragMode::EndMarker)
            {
                if (g_cutStartTime >= 0 && seekTime <= g_cutStartTime)
                    seekTime = g_cutStartTime + 0.01;
                g_cutEndTime = seekTime;
                UpdateCutInfoLabel(GetParent(hwnd));
            }

            UpdateCutTimeEdits();
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
            return 0;
        }
        // Hovering without dragging — redraw for tooltip update
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_LBUTTONUP:
        // Stop arrow scrolling if it was active
        if (g_scrollArrowPressed != ScrollArrowState::None)
        {
            KillTimer(hwnd, SCROLL_ARROW_TIMER_ID);
            ReleaseCapture();
            g_scrollArrowPressed = ScrollArrowState::None;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        
        if (g_isTimelineDragging && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            ReleaseCapture();
            g_isTimelineDragging = false;
            RECT rc; GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            if (x < 0) x = 0; if (x > rc.right) x = rc.right;
            double dur = g_videoPlayer->GetDuration();
            double seekTime = PixelToTime(x, rc, dur);
            
            // Clamp seekTime to valid range [0, duration)
            if (seekTime < 0.0) seekTime = 0.0;
            if (dur > 0.0 && seekTime >= dur) {
                // Clamp to just before the end (last frame)
                double frameTime = g_videoPlayer->frameRate > 0 ? (1.0 / g_videoPlayer->frameRate) : 0.033;
                seekTime = dur - frameTime;
                if (seekTime < 0.0) seekTime = 0.0;
            }

            if (g_timelineDragMode == DragMode::Cursor)
            {
                // Pin cursor to the drop position during the final refinement pass.
                g_previewSeekTime = seekTime;
                if (!g_wasPlayingBeforeDrag)
                    g_videoPlayer->SeekToTime(seekTime, INT_MAX, false, false);
                else
                    g_videoPlayer->SeekWhilePlaying(seekTime);
            }
            else if (g_timelineDragMode == DragMode::Keyframe)
            {
                // Keyframe drag is complete
                g_draggedKeyframeTime = -1.0;
                
                if (!g_videoPlayer->IsPlaying())
                {
                    if (g_videoPlayer->UpdateCropForTime(g_videoPlayer->GetCurrentTime()))
                        g_videoPlayer->ForceRedraw();
                }
            }
            else if (g_timelineDragMode == DragMode::StartMarker)
            {
                if (g_cutEndTime >= 0 && seekTime >= g_cutEndTime)
                    seekTime = g_cutEndTime - 0.01;
                if (seekTime < 0) seekTime = 0;
                g_cutStartTime = seekTime;
                UpdateCutInfoLabel(GetParent(hwnd));
            }
            else if (g_timelineDragMode == DragMode::EndMarker)
            {
                if (g_cutStartTime >= 0 && seekTime <= g_cutStartTime)
                    seekTime = g_cutStartTime + 0.01;
                g_cutEndTime = seekTime;
                UpdateCutInfoLabel(GetParent(hwnd));
            }

            g_timelineDragMode = DragMode::None;
            UpdateCutTimeEdits();
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
            return 0;
        }
        break;
    case WM_RBUTTONUP:
        if (g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            RECT rc; GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            if (x < 0) x = 0; if (x > rc.right) x = rc.right;
            double dur = g_videoPlayer->GetDuration();
            if (dur > 0.0 && rc.right > 0)
            {
                auto keys = g_videoPlayer->GetCropKeyframes();
                double selectedTime = -1.0;
                for (const auto& key : keys)
                {
                    if (key.time < 0.0 || key.time > dur)
                        continue;
                    int px = TimeToPixel(key.time, rc, dur);
                    if (std::abs(px - x) <= 6)
                    {
                        selectedTime = key.time;
                        break;
                    }
                }

                if (selectedTime >= 0.0)
                {
                    POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ClientToScreen(hwnd, &pt);
                    HMENU menu = CreatePopupMenu();
                    AppendMenu(menu, MF_STRING, 1, L"Edit Keyframe");
                    AppendMenu(menu, MF_STRING, 2, L"Delete Keyframe");
                    AppendMenu(menu, MF_STRING, 3, L"Move Keyframe");
                    int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
                    DestroyMenu(menu);
                    if (cmd == 1)
                    {
                        // Edit Keyframe: seek to the exact keyframe timestamp
                        g_videoPlayer->SeekToTimeExact(selectedTime);
                        InvalidateRect(hwnd, NULL, FALSE);
                        UpdateControls();
                        UpdateTimeline();
                    }
                    else if (cmd == 2)
                    {
                        // Delete Keyframe
                        if (g_videoPlayer->RemoveCropKeyframe(selectedTime))
                        {
                            g_videoPlayer->UpdateCropForTime(g_videoPlayer->GetCurrentTime());
                            UpdateControls();
                            UpdateTimeline();
                        }
                    }
                    else if (cmd == 3)
                    {
                        g_isContextKeyframeMoveMode = true;
                        g_contextMovingKeyframeTime = selectedTime;
                        SetCapture(hwnd);
                    }
                    return 0;
                }
            }
        }
        break;
    case WM_CAPTURECHANGED:
        if (g_isContextKeyframeMoveMode && (HWND)lParam != hwnd)
        {
            g_isContextKeyframeMoveMode = false;
            g_contextMovingKeyframeTime = -1.0;
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
        }
        break;
    case WM_MOUSELEAVE:
        g_timelineHoverX = -1;
        g_timelineHoverTime = -1.0;
        g_timelineMouseTracking = false;
        if (g_timecodeTooltipWnd)
            ShowWindow(g_timecodeTooltipWnd, SW_HIDE);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
    {
        if (g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            RECT rc; GetClientRect(hwnd, &rc);
            double dur = g_videoPlayer->GetDuration();
            
            // Get mouse position to zoom around that point
            int x = GET_X_LPARAM(lParam);
            POINT pt = { x, GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            
            // Determine zoom anchor: if cursor is very near start/end, anchor to that boundary
            double zoomAnchorPixel = pt.x;
            const double EDGE_THRESHOLD = 0.1;  // 10% of timeline width
            if (pt.x < rc.right * EDGE_THRESHOLD)
            {
                zoomAnchorPixel = 0.0;  // Anchor to start
            }
            else if (pt.x > rc.right * (1.0 - EDGE_THRESHOLD))
            {
                zoomAnchorPixel = rc.right;  // Anchor to end
            }
            
            // Calculate the time at the anchor point before zoom
            double timeAtAnchor = PixelToTime((int)zoomAnchorPixel, rc, dur);
            
            // Adjust zoom level (1.0 = min, 500.0 = max for deep zooming)
            double oldZoom = g_timelineZoomLevel;
            if (wheelDelta > 0)
            {
                g_timelineZoomLevel *= 1.2;  // Zoom in
                if (g_timelineZoomLevel > 500.0) g_timelineZoomLevel = 500.0;
            }
            else
            {
                g_timelineZoomLevel /= 1.2;  // Zoom out
                if (g_timelineZoomLevel < 1.0) g_timelineZoomLevel = 1.0;
            }
            
            // Adjust scroll offset to keep the same time at the anchor point
            if (g_timelineZoomLevel > 1.0)
            {
                double timeRange = dur / g_timelineZoomLevel;
                g_timelineScrollOffset = timeAtAnchor - (zoomAnchorPixel / (double)rc.right) * timeRange;
                
                // Clamp scroll offset to valid range
                double maxOffset = dur - timeRange;
                if (g_timelineScrollOffset < 0) g_timelineScrollOffset = 0;
                if (g_timelineScrollOffset > maxOffset) g_timelineScrollOffset = maxOffset;
            }
            else
            {
                g_timelineScrollOffset = 0.0;
            }
            
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
            return 0;
        }
        break;
    }
    case WM_TIMER:
    {
        if (wParam == SCROLL_ARROW_TIMER_ID && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            if (g_scrollArrowPressed == ScrollArrowState::LeftArrow)
            {
                double dur = g_videoPlayer->GetDuration();
                double timeRange = dur / g_timelineZoomLevel;
                g_timelineScrollOffset -= timeRange * 0.1;  // Scroll left by 10%
                if (g_timelineScrollOffset < 0) g_timelineScrollOffset = 0;
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateControls();
                return 0;
            }
            else if (g_scrollArrowPressed == ScrollArrowState::RightArrow)
            {
                double dur = g_videoPlayer->GetDuration();
                double timeRange = dur / g_timelineZoomLevel;
                double maxOffset = dur - timeRange;
                g_timelineScrollOffset += timeRange * 0.1;  // Scroll right by 10%
                if (g_timelineScrollOffset > maxOffset) g_timelineScrollOffset = maxOffset;
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateControls();
                return 0;
            }
        }
        break;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(70,70,70));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        
        // Draw scroll arrows if zoomed in
        const int ARROW_WIDTH = 20;
        if (g_timelineZoomLevel > 1.0)
        {
            // Draw left arrow if not at start
            if (g_timelineScrollOffset > 0)
            {
                RECT leftArrowRect = { 0, 0, ARROW_WIDTH, rc.bottom };
                HBRUSH arrowBrush = CreateSolidBrush(RGB(150, 150, 150));
                FillRect(hdc, &leftArrowRect, arrowBrush);
                DeleteObject(arrowBrush);
                
                // Draw "<" character
                SetTextColor(hdc, RGB(0, 0, 0));
                SetBkMode(hdc, TRANSPARENT);
                HFONT font = CreateFont(14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
                HGDIOBJ oldFont = SelectObject(hdc, font);
                TextOutW(hdc, 5, rc.bottom / 2 - 7, L"<", 1);
                SelectObject(hdc, oldFont);
                DeleteObject(font);
            }
            
            // Draw right arrow if not at end
            double dur = g_videoPlayer && g_videoPlayer->IsLoaded() ? g_videoPlayer->GetDuration() : 0;
            if (dur > 0)
            {
                double timeRange = dur / g_timelineZoomLevel;
                double maxOffset = dur - timeRange;
                if (g_timelineScrollOffset < maxOffset)
                {
                    RECT rightArrowRect = { rc.right - ARROW_WIDTH, 0, rc.right, rc.bottom };
                    HBRUSH arrowBrush = CreateSolidBrush(RGB(150, 150, 150));
                    FillRect(hdc, &rightArrowRect, arrowBrush);
                    DeleteObject(arrowBrush);
                    
                    // Draw ">" character
                    SetTextColor(hdc, RGB(0, 0, 0));
                    SetBkMode(hdc, TRANSPARENT);
                    HFONT font = CreateFont(14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
                    HGDIOBJ oldFont = SelectObject(hdc, font);
                    TextOutW(hdc, rc.right - ARROW_WIDTH + 5, rc.bottom / 2 - 7, L">", 1);
                    SelectObject(hdc, oldFont);
                    DeleteObject(font);
                }
            }
        }
        
        if (g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            double dur = g_videoPlayer->GetDuration();
            HBRUSH segmentBrush = CreateSolidBrush(RGB(35, 145, 95));
            HBRUSH selectedSegmentBrush = CreateSolidBrush(RGB(70, 210, 135));
            for (size_t i = 0; g_enableMultiClipEditing && i < g_cutSegments.size(); ++i)
            {
                const auto& segment = g_cutSegments[i];
                int sx = TimeToPixel(segment.start, rc, dur);
                int ex = TimeToPixel(segment.end, rc, dur);
                RECT segmentRect = { sx, std::max(0L, rc.bottom - 8), std::max(sx + 1, ex), rc.bottom };
                FillRect(hdc, &segmentRect,
                         static_cast<int>(i) == g_selectedCutSegment ? selectedSegmentBrush : segmentBrush);
            }
            DeleteObject(segmentBrush);
            DeleteObject(selectedSegmentBrush);

            double cur = (g_previewSeekTime >= 0.0) ? g_previewSeekTime : g_videoPlayer->GetCurrentTime();
            int x = TimeToPixel(cur, rc, dur);
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(200,0,0));
            HGDIOBJ old = SelectObject(hdc, pen);
            MoveToEx(hdc, x, 0, NULL);
            LineTo(hdc, x, rc.bottom);
            SelectObject(hdc, old);
            DeleteObject(pen);

            if (g_cutStartTime >= 0)
            {
                int sx = TimeToPixel(g_cutStartTime, rc, dur);
                pen = CreatePen(PS_SOLID, 1, RGB(0,200,0));
                old = SelectObject(hdc, pen);
                MoveToEx(hdc, sx, 0, NULL);
                LineTo(hdc, sx, rc.bottom);
                SelectObject(hdc, old);
                DeleteObject(pen);
            }
            if (g_cutEndTime >= 0)
            {
                int ex = TimeToPixel(g_cutEndTime, rc, dur);
                pen = CreatePen(PS_SOLID, 1, RGB(0,0,200));
                old = SelectObject(hdc, pen);
                MoveToEx(hdc, ex, 0, NULL);
                LineTo(hdc, ex, rc.bottom);
                SelectObject(hdc, old);
                DeleteObject(pen);
            }

            auto cropKeys = g_videoPlayer->GetCropKeyframes();
            if (!cropKeys.empty())
            {
                HPEN activePen = CreatePen(PS_SOLID, 1, RGB(255,215,0));
                HBRUSH activeBrush = CreateSolidBrush(RGB(255,215,0));
                HPEN disabledPen = CreatePen(PS_SOLID, 1, RGB(180,180,180));
                HGDIOBJ oldPen = SelectObject(hdc, activePen);
                HGDIOBJ oldBrush = SelectObject(hdc, activeBrush);

                for (const auto& key : cropKeys)
                {
                    if (key.time < 0.0 || key.time > dur)
                        continue;
                    int px = TimeToPixel(key.time, rc, dur);
                    if (px >= 0 && px < rc.right)  // Only draw if visible in zoomed view
                    {
                        POINT pts[3] = {
                            { px, 0 },
                            { px - 4, 8 },
                            { px + 4, 8 }
                        };
                        if (key.enabled)
                        {
                            SelectObject(hdc, activePen);
                            SelectObject(hdc, activeBrush);
                            Polygon(hdc, pts, 3);
                        }
                        else
                        {
                            SelectObject(hdc, disabledPen);
                            POINT outline[4] = {
                                { px, 0 },
                                { px - 4, 8 },
                                { px + 4, 8 },
                                { px, 0 }
                            };
                            Polyline(hdc, outline, 4);
                        }
                    }
                }

                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(activePen);
                DeleteObject(activeBrush);
                DeleteObject(disabledPen);
            }
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
