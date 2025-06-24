// main.cpp
#include <windows.h>
#include <commdlg.h>  // For file dialog
#include <commctrl.h> // For common controls
#include "video_player.h"
#include <string>
#include <cstdio> // For swprintf_s

// Control IDs
#define ID_BUTTON_OPEN 1001
#define ID_BUTTON_PLAY 1002
#define ID_BUTTON_PAUSE 1003
#define ID_BUTTON_STOP 1004
#define ID_SLIDER_SEEK 1005
#define ID_TIMER_UPDATE 1006

// Global variables
VideoPlayer *g_videoPlayer = nullptr;
HWND g_hButtonOpen, g_hButtonPlay, g_hButtonPause, g_hButtonStop;
HWND g_hSliderSeek;
HWND g_hStatusText;

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void OpenVideoFile(HWND hwnd);
void UpdateControls();
void UpdateSeekBar();

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        CreateControls(hwnd);
        g_videoPlayer = new VideoPlayer(hwnd);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)g_videoPlayer);
        SetTimer(hwnd, ID_TIMER_UPDATE, 100, nullptr); // Update every 100ms
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_BUTTON_OPEN:
            OpenVideoFile(hwnd);
            break;
        case ID_BUTTON_PLAY:
            if (g_videoPlayer && g_videoPlayer->IsLoaded())
            {
                g_videoPlayer->Play();
                UpdateControls();
            }
            break;
        case ID_BUTTON_PAUSE:
            if (g_videoPlayer)
            {
                g_videoPlayer->Pause();
                UpdateControls();
            }
            break;
        case ID_BUTTON_STOP:
            if (g_videoPlayer)
            {
                g_videoPlayer->Stop();
                UpdateControls();
                UpdateSeekBar();
            }
            break;
        }
        break;

    case WM_HSCROLL:
        if ((HWND)lParam == g_hSliderSeek && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            int pos = SendMessage(g_hSliderSeek, TBM_GETPOS, 0, 0);
            double duration = g_videoPlayer->GetDuration();
            double seekTime = (pos / 100.0) * duration;
            g_videoPlayer->SeekToTime(seekTime);
            UpdateControls();
        }
        break;

    case WM_TIMER:
        if (wParam == ID_TIMER_UPDATE)
        {
            UpdateSeekBar();
            UpdateControls();
        }
        break;

    case WM_SIZE:
    {
        if (g_videoPlayer)
        {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            // Resize video area
            g_videoPlayer->SetPosition(
                10, 50,
                clientRect.right - 20,
                clientRect.bottom - 150);

            // Reposition slider
            MoveWindow(
                g_hSliderSeek,
                10,
                clientRect.bottom - 80,
                clientRect.right - 20,
                30,
                TRUE);

            // Reposition status text
            MoveWindow(
                g_hStatusText,
                10,
                clientRect.bottom - 40,
                clientRect.right - 20,
                20,
                TRUE);
        }
    }
    break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        if (g_videoPlayer)
        {
            delete g_videoPlayer;
            g_videoPlayer = nullptr;
        }
        KillTimer(hwnd, ID_TIMER_UPDATE);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void CreateControls(HWND hwnd)
{
    InitCommonControls();

    // Open button
    g_hButtonOpen = CreateWindow(
        L"BUTTON", L"Open Video",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        10, 10, 100, 30,
        hwnd, (HMENU)ID_BUTTON_OPEN,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Play button
    g_hButtonPlay = CreateWindow(
        L"BUTTON", L"Play",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        120, 10, 60, 30,
        hwnd, (HMENU)ID_BUTTON_PLAY,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Pause button
    g_hButtonPause = CreateWindow(
        L"BUTTON", L"Pause",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        190, 10, 60, 30,
        hwnd, (HMENU)ID_BUTTON_PAUSE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Stop button
    g_hButtonStop = CreateWindow(
        L"BUTTON", L"Stop",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        260, 10, 60, 30,
        hwnd, (HMENU)ID_BUTTON_STOP,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    // Seek slider
    g_hSliderSeek = CreateWindow(
        TRACKBAR_CLASS, L"Seek",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_BOTH,
        10, clientRect.bottom - 80,
        clientRect.right - 20, 30,
        hwnd, (HMENU)ID_SLIDER_SEEK,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    SendMessage(g_hSliderSeek, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessage(g_hSliderSeek, TBM_SETPOS, TRUE, 0);

    // Status text
    g_hStatusText = CreateWindow(
        L"STATIC", L"No video loaded",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, clientRect.bottom - 40,
        clientRect.right - 20, 20,
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Disable controls until video is loaded
    EnableWindow(g_hButtonPlay, FALSE);
    EnableWindow(g_hButtonPause, FALSE);
    EnableWindow(g_hButtonStop, FALSE);
    EnableWindow(g_hSliderSeek, FALSE);
}

void OpenVideoFile(HWND hwnd)
{
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Video Files\0*.mp4;*.avi;*.mov;*.mkv;*.wmv;*.flv;*.webm;*.m4v;*.3gp\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn))
    {
        if (g_videoPlayer && g_videoPlayer->LoadVideo(std::wstring(szFile)))
        {
            SetWindowTextW(g_hStatusText, (L"Loaded: " + std::wstring(szFile)).c_str());
            EnableWindow(g_hButtonPlay, TRUE);
            EnableWindow(g_hButtonPause, TRUE);
            EnableWindow(g_hButtonStop, TRUE);
            EnableWindow(g_hSliderSeek, TRUE);
            UpdateControls();
            UpdateSeekBar();
        }
        else
        {
            SetWindowTextW(g_hStatusText, L"Failed to load video file");
            MessageBoxW(hwnd, L"Failed to load the video file. Please check FFmpeg setup.", L"Error", MB_OK | MB_ICONERROR);
        }
    }
}

void UpdateControls()
{
    if (!g_videoPlayer)
        return;

    bool isLoaded = g_videoPlayer->IsLoaded();
    bool isPlaying = g_videoPlayer->IsPlaying();

    EnableWindow(g_hButtonPlay, isLoaded && !isPlaying);
    EnableWindow(g_hButtonPause, isLoaded && isPlaying);
    EnableWindow(g_hButtonStop, isLoaded);
    EnableWindow(g_hSliderSeek, isLoaded);

    if (isLoaded)
    {
        double currentTime = g_videoPlayer->GetCurrentTime();
        double duration = g_videoPlayer->GetDuration();
        wchar_t statusText[256];
        swprintf_s(statusText, _countof(statusText),
                   L"Time: %.2fs / %.2fs | Frame: %lld / %lld | %s",
                   currentTime, duration,
                   g_videoPlayer->GetCurrentFrame(), g_videoPlayer->GetTotalFrames(),
                   isPlaying ? L"Playing" : L"Paused");
        SetWindowTextW(g_hStatusText, statusText);
    }
}

void UpdateSeekBar()
{
    if (!g_videoPlayer || !g_videoPlayer->IsLoaded())
        return;
    double currentTime = g_videoPlayer->GetCurrentTime();
    double duration = g_videoPlayer->GetDuration();
    if (duration > 0)
    {
        int percentage = (int)((currentTime / duration) * 100);
        SendMessage(g_hSliderSeek, TBM_SETPOS, TRUE, percentage);
    }
}

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"VideoEditorClass";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"Video Editor - Preview",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
        return 0;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
