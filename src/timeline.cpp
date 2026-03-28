#include "timeline.h"
#include "video_player.h"
#include "ui_updates.h"
#include <windowsx.h>
#include <cmath>

// Forward declarations
void UpdateControls();
void UpdateCutInfoLabel(HWND hwnd);
void UpdateCutTimeEdits();

// Global variables
extern VideoPlayer *g_videoPlayer;
extern double g_cutStartTime, g_cutEndTime;
extern bool g_isTimelineDragging;
extern bool g_wasPlayingBeforeDrag;
enum class DragMode { None, Cursor, StartMarker, EndMarker, Keyframe };
extern DragMode g_timelineDragMode;
extern double g_draggedKeyframeTime;
double g_previewSeekTime = -1.0; // For immediate timeline feedback

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
            if (g_wasPlayingBeforeDrag)
                g_videoPlayer->Pause();
            
            // Immediate UI update
            g_previewSeekTime = seekTime;
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateWindow(hwnd); // Force restart of paint cycle to draw line immediately
            
            g_videoPlayer->SeekToTime(seekTime, 0);
            
            g_previewSeekTime = -1.0; // Reset after seek

            g_isTimelineDragging = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
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
                
                g_videoPlayer->SeekToTime(seekTime, 0);
                
                g_previewSeekTime = -1.0;
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
                g_videoPlayer->SeekToTime(seekTime, 0);
                if (g_wasPlayingBeforeDrag)
                    g_videoPlayer->Play();
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
