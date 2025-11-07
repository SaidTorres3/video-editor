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
enum class DragMode { None, Cursor, StartMarker, EndMarker };
extern DragMode g_timelineDragMode;

LRESULT CALLBACK TimelineProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
        if (g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            SetFocus(hwnd);
            RECT rc; GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            if (x < 0) x = 0; if (x > rc.right) x = rc.right;
            double ratio = rc.right > 0 ? (x / (double)rc.right) : 0.0;
            double dur = g_videoPlayer->GetDuration();
            double seekTime = ratio * dur;

            int startX = (g_cutStartTime >= 0 && dur > 0) ? (int)((g_cutStartTime / dur) * rc.right) : -1000;
            int endX = (g_cutEndTime >= 0 && dur > 0) ? (int)((g_cutEndTime / dur) * rc.right) : -1000;
            int margin = 5;
            if (std::abs(x - startX) <= margin)
            {
                g_timelineDragMode = DragMode::StartMarker;
            }
            else if (std::abs(x - endX) <= margin)
            {
                g_timelineDragMode = DragMode::EndMarker;
            }
            else
            {
                if (g_videoPlayer->IsClipPreviewActive())
                {
                    if (seekTime < g_cutStartTime || seekTime > g_cutEndTime)
                    {
                        g_videoPlayer->CancelClipPreview();
                    }
                    else
                    {
                        g_videoPlayer->SeekToTime(seekTime);
                        InvalidateRect(hwnd, NULL, FALSE);
                        UpdateControls();
                        return 0;
                    }
                }
                g_timelineDragMode = DragMode::Cursor;
                g_wasPlayingBeforeDrag = g_videoPlayer->IsPlaying();
                if (g_wasPlayingBeforeDrag)
                    g_videoPlayer->Pause();
                g_videoPlayer->SeekToTime(seekTime);
            }

            g_isTimelineDragging = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (g_isTimelineDragging && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            RECT rc; GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            if (x < 0) x = 0; if (x > rc.right) x = rc.right;
            double ratio = rc.right > 0 ? (x / (double)rc.right) : 0.0;
            double dur = g_videoPlayer->GetDuration();
            double seekTime = ratio * dur;

            if (g_timelineDragMode == DragMode::Cursor)
            {
                g_videoPlayer->SeekToTime(seekTime);
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
        if (g_isTimelineDragging && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            ReleaseCapture();
            g_isTimelineDragging = false;
            RECT rc; GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            if (x < 0) x = 0; if (x > rc.right) x = rc.right;
            double ratio = rc.right > 0 ? (x / (double)rc.right) : 0.0;
            double dur = g_videoPlayer->GetDuration();
            double seekTime = ratio * dur;

            if (g_timelineDragMode == DragMode::Cursor)
            {
                g_videoPlayer->SeekToTime(seekTime);
                if (g_wasPlayingBeforeDrag)
                    g_videoPlayer->Play();
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
                    int px = static_cast<int>((key.time / dur) * rc.right);
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
                    AppendMenu(menu, MF_STRING, 1, L"Delete Keyframe");
                    int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
                    DestroyMenu(menu);
                    if (cmd == 1)
                    {
                        if (g_videoPlayer->RemoveCropKeyframe(selectedTime))
                        {
                            g_videoPlayer->UpdateCropForTime(g_videoPlayer->GetCurrentTime());
                            UpdateControls();
                            UpdateTimeline();
                        }
                    }
                    return 0;
                }
            }
        }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(70,70,70));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        if (g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            double dur = g_videoPlayer->GetDuration();
            double cur = g_videoPlayer->GetCurrentTime();
            int width = rc.right;
            int x = (dur > 0) ? (int)((cur / dur) * width) : 0;
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(200,0,0));
            HGDIOBJ old = SelectObject(hdc, pen);
            MoveToEx(hdc, x, 0, NULL);
            LineTo(hdc, x, rc.bottom);
            SelectObject(hdc, old);
            DeleteObject(pen);

            if (g_cutStartTime >= 0)
            {
                int sx = (int)((g_cutStartTime / dur) * width);
                pen = CreatePen(PS_SOLID, 1, RGB(0,200,0));
                old = SelectObject(hdc, pen);
                MoveToEx(hdc, sx, 0, NULL);
                LineTo(hdc, sx, rc.bottom);
                SelectObject(hdc, old);
                DeleteObject(pen);
            }
            if (g_cutEndTime >= 0)
            {
                int ex = (int)((g_cutEndTime / dur) * width);
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
                    int px = (int)((key.time / dur) * width);
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
