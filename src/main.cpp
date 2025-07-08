// main.cpp
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>  // For file dialog
#include <commctrl.h> // For common controls
#include <shellapi.h> // For drag-and-drop
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#include "video_player.h"
#include "options_window.h"
#include "progress_window.h"
#include "window_proc.h"
#include "timeline.h"
#include "utils.h"

#include <string>
#include <cstdlib>
#include <cstdio> // For swprintf_s
#include <thread>

// Control IDs
#define ID_TIMER_UPDATE 1006

// Global variables
VideoPlayer *g_videoPlayer = nullptr;
HWND g_hButtonOpen, g_hButtonPlay, g_hButtonPause, g_hButtonStop;
HWND g_hTimeline;
HWND g_hStatusText;
HWND g_hListBoxAudioTracks, g_hButtonMuteTrack;
HWND g_hSliderTrackVolume, g_hSliderMasterVolume;
HWND g_hLabelAudioTracks, g_hLabelTrackVolume, g_hLabelMasterVolume, g_hLabelEditing;
HWND g_hButtonSetStart, g_hButtonSetEnd, g_hButtonCut, g_hCheckboxMergeAudio;
HWND g_hRadioCopyCodec, g_hRadioH264, g_hEditBitrate;
HWND g_hEditStartTime, g_hEditEndTime;
HWND g_hLabelCutInfo;
HWND g_hButtonOptions;
double g_cutStartTime = -1.0;
double g_cutEndTime = -1.0;
bool g_isTimelineDragging = false;
bool g_wasPlayingBeforeDrag = false;
enum class DragMode { None, Cursor, StartMarker, EndMarker };
DragMode g_timelineDragMode = DragMode::None;

// Dark mode UI resources
HFONT g_hFont = nullptr;
HBRUSH g_hbrBackground = nullptr;
COLORREF g_textColor = RGB(240, 240, 240);

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    LoadSettings();
    const wchar_t CLASS_NAME[] = L"VideoEditorClass";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    RegisterClass(&wc);

    WNDCLASS twc = {};
    twc.lpfnWndProc = TimelineProc;
    twc.hInstance = hInstance;
    twc.lpszClassName = L"TimelineClass";
    twc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    twc.hbrBackground = nullptr; // custom paint
    RegisterClass(&twc);

    WNDCLASS owc = {};
    owc.lpfnWndProc = OptionsProc;
    owc.hInstance = hInstance;
    owc.lpszClassName = L"OptionsClass";
    owc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    owc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&owc);

    WNDCLASS pwc = {};
    pwc.lpfnWndProc = ProgressProc;
    pwc.hInstance = hInstance;
    pwc.lpszClassName = L"ProgressClass";
    pwc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    pwc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&pwc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"Video Editor - Preview",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
        return 0;

    // Enable immersive dark mode for the window
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    ApplyDarkTheme(hwnd);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            bool handled = true;
            switch (msg.wParam)
            {
            case VK_SPACE:
                if (g_videoPlayer->IsPlaying())
                    g_videoPlayer->Pause();
                else
                    g_videoPlayer->Play();
                break;
            case VK_LEFT:
            case 'J':
            case 'j':
            {
                double offset = (msg.wParam == VK_LEFT) ? 5.0 : 10.0;
                double t = g_videoPlayer->GetCurrentTime() - offset;
                if (t < 0.0) t = 0.0;
                bool wasPlaying = g_videoPlayer->IsPlaying();
                if (wasPlaying)
                    g_videoPlayer->Pause();
                g_videoPlayer->SeekToTime(t);
                if (wasPlaying)
                    g_videoPlayer->Play();
                break;
            }
            case VK_RIGHT:
            case 'L':
            case 'l':
            {
                double offset = (msg.wParam == VK_RIGHT) ? 5.0 : 10.0;
                double t = g_videoPlayer->GetCurrentTime() + offset;
                double dur = g_videoPlayer->GetDuration();
                if (t > dur) t = dur;
                bool wasPlaying = g_videoPlayer->IsPlaying();
                if (wasPlaying)
                    g_videoPlayer->Pause();
                g_videoPlayer->SeekToTime(t);
                if (wasPlaying)
                    g_videoPlayer->Play();
                break;
            }
            case 'K':
            case 'k':
                g_videoPlayer->Pause();
                break;
            case VK_OEM_COMMA:
            {
                int64_t frame = g_videoPlayer->GetCurrentFrame() - 1;
                if (frame < 0) frame = 0;
                bool wasPlaying = g_videoPlayer->IsPlaying();
                if (wasPlaying)
                    g_videoPlayer->Pause();
                g_videoPlayer->SeekToFrame(frame);
                if (wasPlaying)
                    g_videoPlayer->Play();
                break;
            }
            case VK_OEM_PERIOD:
            {
                int64_t frame = g_videoPlayer->GetCurrentFrame() + 1;
                int64_t maxf = g_videoPlayer->GetTotalFrames() - 1;
                if (frame > maxf) frame = maxf;
                bool wasPlaying = g_videoPlayer->IsPlaying();
                if (wasPlaying)
                    g_videoPlayer->Pause();
                g_videoPlayer->SeekToFrame(frame);
                if (wasPlaying)
                    g_videoPlayer->Play();
                break;
            }
            default:
                handled = false;
                break;
            }
            if (handled)
            {
                // UpdateControls();
                // UpdateTimeline();
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}