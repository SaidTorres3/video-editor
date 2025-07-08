#include "timeline.h"
#include "video_player.h"
#include "ui_updates.h"
#include <windowsx.h>

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
            if (abs(x - startX) <= margin)
            {
                g_timelineDragMode = DragMode::StartMarker;
            }
            else if (abs(x - endX) <= margin)
            {
                g_timelineDragMode = DragMode::EndMarker;
            }
            else
            {
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
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
