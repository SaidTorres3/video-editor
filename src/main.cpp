// main.cpp
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>  // For file dialog
#include <objbase.h>  // For CoInitializeEx
#include <commctrl.h> // For common controls
#include <shellapi.h> // For drag-and-drop
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "video_player.h"
#include "options_window.h"
#include "progress_window.h"
#include "upload_dialog.h"
#include <curl/curl.h>
#include "window_proc.h"
#include "timeline.h"
#include "utils.h"
#include "file_handling.h"
#include "ui_updates.h"

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
HWND g_hRadioUseBitrate, g_hRadioUseSize;
HWND g_hLabelBitrate;
HWND g_hEditTargetSize;
HWND g_hLabelTargetSize;
HWND g_hEditStartTime, g_hEditEndTime, g_hButtonPlayClip, g_hButtonPlayEnd;
HWND g_hLabelCutInfo;
HWND g_hButtonOptions;
HWND g_hButtonTogglePanel;
bool g_isPanelVisible = true;
double g_cutStartTime = -1.0;
double g_cutEndTime = -1.0;
bool g_isTimelineDragging = false;
bool g_wasPlayingBeforeDrag = false;
bool g_resumePlayAfterSeek = false;
enum class DragMode { None, Cursor, StartMarker, EndMarker, Keyframe };
DragMode g_timelineDragMode = DragMode::None;
double g_draggedKeyframeTime = -1.0;  // Time of the keyframe being dragged
extern double g_previewSeekTime;

// Dark mode UI resources
HFONT g_hFont = nullptr;
HBRUSH g_hbrBackground = nullptr;
COLORREF g_textColor = RGB(240, 240, 240);

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadSettings();
    curl_global_init(CURL_GLOBAL_DEFAULT);
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

    WNDCLASS b2c = {};
    b2c.lpfnWndProc = B2ConfigProc;
    b2c.hInstance = hInstance;
    b2c.lpszClassName = L"B2ConfigClass";
    b2c.hCursor = LoadCursor(nullptr, IDC_ARROW);
    b2c.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&b2c);

    WNDCLASS upc = {};
    upc.lpfnWndProc = UploadProc;
    upc.hInstance = hInstance;
    upc.lpszClassName = L"UploadConfigClass";
    upc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    upc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&upc);

    WNDCLASS catc = {};
    catc.lpfnWndProc = CatboxConfigProc;
    catc.hInstance = hInstance;
    catc.lpszClassName = L"CatboxConfigClass";
    catc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    catc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&catc);

    WNDCLASS pwc = {};
    pwc.lpfnWndProc = ProgressProc;
    pwc.hInstance = hInstance;
    pwc.lpszClassName = L"ProgressClass";
    pwc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    pwc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&pwc);

    WNDCLASS ucw = {};
    ucw.lpfnWndProc = UrlCopyProc;
    ucw.hInstance = hInstance;
    ucw.lpszClassName = L"UrlCopyClass";
    ucw.hCursor = LoadCursor(nullptr, IDC_ARROW);
    ucw.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&ucw);

    WNDCLASS muc = {};
    muc.lpfnWndProc = ManualUploadProc;
    muc.hInstance = hInstance;
    muc.lpszClassName = L"ManualUploadClass";
    muc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    muc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&muc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"Video Editor - Preview",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
        return 0;

    ApplyDarkTheme(hwnd);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1)
    {
        LoadVideoFile(hwnd, std::wstring(argv[1]));
    }
    if (argv)
        LocalFree(argv);

    MSG msg = {};
    UINT_PTR g_exactSeekTimer = 0;
    while (GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_TIMER && msg.wParam == g_exactSeekTimer && g_exactSeekTimer != 0)
        {
            KillTimer(nullptr, g_exactSeekTimer);
            g_exactSeekTimer = 0;
            if (g_previewSeekTime >= 0.0 && g_videoPlayer && g_videoPlayer->IsLoaded())
            {
                bool wasPlaying = g_videoPlayer->IsPlaying();
                if (wasPlaying)
                    g_videoPlayer->Pause();
                g_videoPlayer->SeekToTimeExact(g_previewSeekTime);
                if (wasPlaying)
                    g_videoPlayer->Play();
                g_previewSeekTime = -1.0;
            }
            continue;
        }

        if ((msg.message == WM_KEYDOWN || msg.message == WM_KEYUP) && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            HWND focused = GetFocus();
            bool isEdit = false;
            if (focused)
            {
                wchar_t className[32];
                GetClassNameW(focused, className, sizeof(className) / sizeof(wchar_t));
                if (lstrcmpW(className, L"Edit") == 0)
                    isEdit = true;
            }
            if (!isEdit)
            {
                if (msg.message == WM_KEYUP)
                {
                    bool handled = true;
                    switch (msg.wParam)
                    {
                    case VK_LEFT:
                    case 'J':
                    case 'j':
                    case VK_RIGHT:
                    case 'L':
                    case 'l':
                        if (g_previewSeekTime >= 0.0)
                        {
                            if (g_exactSeekTimer != 0)
                                KillTimer(nullptr, g_exactSeekTimer);
                            g_exactSeekTimer = SetTimer(nullptr, 0, 1, nullptr);
                        }
                        break;
                    default:
                        handled = false;
                        break;
                    }
                    if (handled) continue;
                }
                else if (msg.message == WM_KEYDOWN)
                {
                    double speedMultiplier = (GetKeyState(VK_SHIFT) & 0x8000) ? 10.0 : 1.0;

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
                        if (g_exactSeekTimer != 0)
                        {
                            KillTimer(nullptr, g_exactSeekTimer);
                            g_exactSeekTimer = 0;
                        }

                        int repeatCount = msg.lParam & 0xFFFF;
                        bool isRepeated = (msg.lParam & (1 << 30)) != 0;
                        double currentBase = (g_previewSeekTime >= 0.0) ? g_previewSeekTime : g_videoPlayer->GetCurrentTime();
                        double offset = ((msg.wParam == VK_LEFT) ? 5.0 : 10.0) * speedMultiplier * repeatCount;
                        double t = currentBase - offset;
                        if (t < 0.0) t = 0.0;

                        g_previewSeekTime = t;
                        if (g_hTimeline) {
                            InvalidateRect(g_hTimeline, NULL, FALSE);
                            UpdateWindow(g_hTimeline);
                        }
                        UpdateControls(); // Force immediate update of time label

                        // Throttle: If another keydown for the same key is pending, skip the seek
                        MSG nextMsg;
                        if (PeekMessage(&nextMsg, nullptr, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE)) {
                            if (nextMsg.wParam == msg.wParam) {
                                continue; // Skip the heavy operation
                            }
                        }

                        bool wasPlaying = g_videoPlayer->IsPlaying();
                        if (wasPlaying)
                            g_videoPlayer->Pause();
                        g_videoPlayer->SeekToTime(t, 0, isRepeated, !isRepeated);
                        if (wasPlaying)
                            g_videoPlayer->Play();
                        
                        break;
                    }
                    case VK_RIGHT:
                    case 'L':
                    case 'l':
                    {
                        if (g_exactSeekTimer != 0)
                        {
                            KillTimer(nullptr, g_exactSeekTimer);
                            g_exactSeekTimer = 0;
                        }

                        int repeatCount = msg.lParam & 0xFFFF;
                        bool isRepeated = (msg.lParam & (1 << 30)) != 0;
                        double currentBase = (g_previewSeekTime >= 0.0) ? g_previewSeekTime : g_videoPlayer->GetCurrentTime();
                        double offset = ((msg.wParam == VK_RIGHT) ? 5.0 : 10.0) * speedMultiplier * repeatCount;
                        double t = currentBase + offset;
                        double dur = g_videoPlayer->GetDuration();
                        if (t > dur) t = dur;

                        g_previewSeekTime = t;
                        if (g_hTimeline) {
                            InvalidateRect(g_hTimeline, NULL, FALSE);
                            UpdateWindow(g_hTimeline);
                        }
                        UpdateControls(); // Force immediate update of time label

                        MSG nextMsg;
                        if (PeekMessage(&nextMsg, nullptr, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE)) {
                            if (nextMsg.wParam == msg.wParam) {
                                continue;
                            }
                        }

                        bool wasPlaying = g_videoPlayer->IsPlaying();
                        if (wasPlaying)
                            g_videoPlayer->Pause();
                        g_videoPlayer->SeekToTime(t, 0, isRepeated, !isRepeated);
                        if (wasPlaying)
                            g_videoPlayer->Play();
                        
                        break;
                    }
                    case 'K':
                    case 'k':
                        if (g_videoPlayer->IsPlaying())
                            g_videoPlayer->Pause();
                        else
                            g_videoPlayer->Play();
                        break;
                    case VK_OEM_COMMA:
                    {
                        // Frame step should pause playback
                        if (g_videoPlayer->IsPlaying())
                            g_videoPlayer->Pause();

                        int64_t frame = g_videoPlayer->GetCurrentFrame() - 1;
                        if (frame < 0) frame = 0;

                        // Throttle: if subsequent keys are waiting, skip this update to remain responsive
                        MSG nextMsg;
                        if (PeekMessage(&nextMsg, nullptr, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE)) {
                            if (nextMsg.wParam == msg.wParam) {
                                continue;
                            }
                        }

                        g_videoPlayer->SeekToFrame(frame);
                        
                        if (g_hTimeline) {
                            InvalidateRect(g_hTimeline, NULL, FALSE);
                        }
                        UpdateControls();
                        break;
                    }
                    case VK_OEM_PERIOD:
                    {
                        // Frame step should pause playback
                        if (g_videoPlayer->IsPlaying())
                            g_videoPlayer->Pause();

                        int64_t frame = g_videoPlayer->GetCurrentFrame() + 1;
                        int64_t total = g_videoPlayer->GetTotalFrames();
                        if (total > 0)
                        {
                            int64_t maxf = total - 1;
                            if (frame > maxf)
                                frame = maxf;
                        }

                        // Throttle: if subsequent keys are waiting, skip this update to remain responsive
                        MSG nextMsg;
                        if (PeekMessage(&nextMsg, nullptr, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE)) {
                            if (nextMsg.wParam == msg.wParam) {
                                continue;
                            }
                        }

                        g_videoPlayer->SeekToFrame(frame);
                        
                        if (g_hTimeline) {
                            InvalidateRect(g_hTimeline, NULL, FALSE);
                        }
                        UpdateControls();
                        break;
                    }
                    default:
                        handled = false;
                        break;
                    }
                    if (handled)
                    {
                        continue;
                    }
                }
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    curl_global_cleanup();
    CoUninitialize();
    return 0;
}
