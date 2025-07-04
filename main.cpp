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
#include <string>
#include <cstdlib>
#include <cstdio> // For swprintf_s

// Control IDs
#define ID_BUTTON_OPEN 1001
#define ID_BUTTON_PLAY 1002
#define ID_BUTTON_PAUSE 1003
#define ID_BUTTON_STOP 1004
#define ID_TIMELINE 1005
#define ID_TIMER_UPDATE 1006
#define ID_LISTBOX_AUDIO_TRACKS 1007
#define ID_BUTTON_MUTE_TRACK 1008
#define ID_SLIDER_TRACK_VOLUME 1009
#define ID_SLIDER_MASTER_VOLUME 1010


// Editing Control IDs
#define ID_BUTTON_SET_START 1011
#define ID_BUTTON_SET_END 1012
#define ID_BUTTON_CUT 1013
#define ID_CHECKBOX_MERGE_AUDIO 1014
#define ID_RADIO_COPY_CODEC 1015
#define ID_RADIO_H264 1016
#define ID_EDIT_BITRATE 1017
#define ID_EDIT_START_TIME 1018
#define ID_EDIT_END_TIME 1019
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
HWND g_hProgressWnd, g_hProgressBar;
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

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void OpenVideoFile(HWND hwnd);
void LoadVideoFile(HWND hwnd, const std::wstring& filename);
void UpdateControls();
void UpdateTimeline();
void UpdateAudioTrackList();
void OnAudioTrackSelectionChanged();
void OnMuteTrackClicked();
void OnTrackVolumeChanged();
void OnMasterVolumeChanged();
void UpdateCutInfoLabel(HWND hwnd);
void OnSetStartClicked(HWND hwnd);
void OnSetEndClicked(HWND hwnd);
void OnCutClicked(HWND hwnd);
std::wstring FormatTime(double totalSeconds, bool showMilliseconds = false);
double ParseTimeString(const std::wstring& str);
void UpdateCutTimeEdits();
void RepositionControls(HWND hwnd);
void ApplyDarkTheme(HWND hwnd);
void ShowProgressWindow(HWND parent);
void UpdateProgress(int percent);
void CloseProgressWindow();
LRESULT CALLBACK TimelineProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);


// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        // Initialize dark theme resources
        g_hbrBackground = CreateSolidBrush(RGB(45, 45, 48));
        g_hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        CreateControls(hwnd);
        g_videoPlayer = new VideoPlayer(hwnd);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)g_videoPlayer);
        SetTimer(hwnd, ID_TIMER_UPDATE, 100, nullptr); // Update every 100ms
        DragAcceptFiles(hwnd, TRUE);

        // Apply dark theme to window itself
        ApplyDarkTheme(hwnd);
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
                UpdateTimeline();
            }
            break;
        case ID_BUTTON_MUTE_TRACK:
            OnMuteTrackClicked();
            break;
        case ID_BUTTON_SET_START:
            OnSetStartClicked(hwnd);
            break;
        case ID_BUTTON_SET_END:
            OnSetEndClicked(hwnd);
            break;
        case ID_BUTTON_CUT:
            OnCutClicked(hwnd);
            break;
        case ID_RADIO_COPY_CODEC:
        case ID_RADIO_H264:
            EnableWindow(g_hEditBitrate, SendMessage(g_hRadioH264, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        }

        if ((HWND)lParam == g_hEditStartTime && HIWORD(wParam) == EN_KILLFOCUS)
        {
            wchar_t buf[64];
            GetWindowTextW(g_hEditStartTime, buf, 64);
            double t = ParseTimeString(buf);
            if (t >= 0)
            {
                g_cutStartTime = t;
                if (g_cutEndTime >= 0 && g_cutStartTime >= g_cutEndTime)
                    g_cutEndTime = -1.0;
            }
            UpdateCutInfoLabel(hwnd);
            UpdateCutTimeEdits();
            UpdateTimeline();
        }
        else if ((HWND)lParam == g_hEditEndTime && HIWORD(wParam) == EN_KILLFOCUS)
        {
            wchar_t buf[64];
            GetWindowTextW(g_hEditEndTime, buf, 64);
            double t = ParseTimeString(buf);
            if (t >= 0)
            {
                g_cutEndTime = t;
                if (g_cutStartTime >= 0 && g_cutEndTime <= g_cutStartTime)
                    g_cutEndTime = -1.0;
            }
            UpdateCutInfoLabel(hwnd);
            UpdateCutTimeEdits();
            UpdateTimeline();
        }

        // Handle listbox selection changes
        if (HIWORD(wParam) == LBN_SELCHANGE && (HWND)lParam == g_hListBoxAudioTracks)
        {
            OnAudioTrackSelectionChanged();
        }
        break;

    case WM_HSCROLL:
        if ((HWND)lParam == g_hSliderTrackVolume)
        {
            OnTrackVolumeChanged();
        }
        else if ((HWND)lParam == g_hSliderMasterVolume)
        {
            OnMasterVolumeChanged();
        }
        break;

    case WM_MOUSEWHEEL:
    {
        HWND focused = GetFocus();
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        double step = 0.1 * (delta / WHEEL_DELTA);
        if (focused == g_hEditStartTime && g_cutStartTime >= 0)
        {
            g_cutStartTime -= step;
            if (g_cutStartTime < 0) g_cutStartTime = 0;
            if (g_cutEndTime >= 0 && g_cutStartTime >= g_cutEndTime)
                g_cutStartTime = g_cutEndTime - 0.01;
            UpdateCutInfoLabel(hwnd);
            UpdateCutTimeEdits();
            UpdateTimeline();
        }
        else if (focused == g_hEditEndTime && g_cutEndTime >= 0)
        {
            g_cutEndTime -= step;
            if (g_cutEndTime < 0) g_cutEndTime = 0;
            if (g_cutStartTime >= 0 && g_cutEndTime <= g_cutStartTime)
                g_cutEndTime = g_cutStartTime + 0.01;
            UpdateCutInfoLabel(hwnd);
            UpdateCutTimeEdits();
            UpdateTimeline();
        }
    }
    break;

    case WM_TIMER:
        if (wParam == ID_TIMER_UPDATE)
        {
            UpdateTimeline();
            UpdateControls();
        }
        break;

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;
        wchar_t filePath[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH))
        {
            LoadVideoFile(hwnd, std::wstring(filePath));
        }
        DragFinish(hDrop);
    }
    break;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_textColor);
        SetBkColor(hdc, RGB(45,45,48));
        return (INT_PTR)g_hbrBackground;
    }
    break;

    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, g_hbrBackground);
        return 1;
    }
    break;

    case WM_SIZE:
    {
        RepositionControls(hwnd);
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
        if (g_hFont)
        {
            DeleteObject(g_hFont);
            g_hFont = nullptr;
        }
        if (g_hbrBackground)
        {
            DeleteObject(g_hbrBackground);
            g_hbrBackground = nullptr;
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
    ApplyDarkTheme(g_hButtonOpen);

    // Play button
    g_hButtonPlay = CreateWindow(
        L"BUTTON", L"Play",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        120, 10, 60, 30,
        hwnd, (HMENU)ID_BUTTON_PLAY,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonPlay);

    // Pause button
    g_hButtonPause = CreateWindow(
        L"BUTTON", L"Pause",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        190, 10, 60, 30,
        hwnd, (HMENU)ID_BUTTON_PAUSE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonPause);

    // Stop button
    g_hButtonStop = CreateWindow(
        L"BUTTON", L"Stop",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        260, 10, 60, 30,
        hwnd, (HMENU)ID_BUTTON_STOP,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonStop);

    // Timeline
    g_hTimeline = CreateWindow(
        L"TimelineClass", nullptr,
        WS_CHILD | WS_VISIBLE,
        10, 370, 600, 30, // Placeholder position
        hwnd, (HMENU)ID_TIMELINE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hTimeline);

    // Status text
    g_hStatusText = CreateWindow(
        L"STATIC", L"No video loaded",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, 410, 600, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hStatusText);

    // Audio controls section
    // Audio tracks label
    g_hLabelAudioTracks = CreateWindow(
        L"STATIC", L"Audio Tracks:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 50, 100, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelAudioTracks);

    // Audio tracks listbox
    g_hListBoxAudioTracks = CreateWindow(
        L"LISTBOX", nullptr,
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        340, 75, 200, 100, // Placeholder position
        hwnd, (HMENU)ID_LISTBOX_AUDIO_TRACKS,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hListBoxAudioTracks);

    // Mute track button
    g_hButtonMuteTrack = CreateWindow(
        L"BUTTON", L"Mute/Unmute",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        340, 185, 100, 25, // Placeholder position
        hwnd, (HMENU)ID_BUTTON_MUTE_TRACK,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonMuteTrack);

    // Track volume label
    g_hLabelTrackVolume = CreateWindow(
        L"STATIC", L"Track Volume:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 220, 100, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelTrackVolume);

    // Track volume slider
    g_hSliderTrackVolume = CreateWindow(
        TRACKBAR_CLASS, L"Track Volume",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_BOTH,
        340, 245, 200, 30, // Placeholder position
        hwnd, (HMENU)ID_SLIDER_TRACK_VOLUME,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    SendMessage(g_hSliderTrackVolume, TBM_SETRANGE, TRUE, MAKELONG(0, 200)); // 0-200% volume
    SendMessage(g_hSliderTrackVolume, TBM_SETPOS, TRUE, 100); // Default 100%
    ApplyDarkTheme(g_hSliderTrackVolume);

    // Master volume label
    g_hLabelMasterVolume = CreateWindow(
        L"STATIC", L"Master Volume:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 285, 100, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelMasterVolume);

    // Master volume slider
    g_hSliderMasterVolume = CreateWindow(
        TRACKBAR_CLASS, L"Master Volume",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_BOTH,
        340, 310, 200, 30, // Placeholder position
        hwnd, (HMENU)ID_SLIDER_MASTER_VOLUME,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    SendMessage(g_hSliderMasterVolume, TBM_SETRANGE, TRUE, MAKELONG(0, 200)); // 0-200% volume
    SendMessage(g_hSliderMasterVolume, TBM_SETPOS, TRUE, 100); // Default 100%
    ApplyDarkTheme(g_hSliderMasterVolume);

    // Editing controls section
    g_hLabelEditing = CreateWindow(
        L"STATIC", L"Editing:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 350, 100, 20, // Placeholder
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelEditing);

    g_hButtonSetStart = CreateWindow(
        L"BUTTON", L"Set Start",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        340, 375, 95, 25, // Placeholder
        hwnd, (HMENU)ID_BUTTON_SET_START,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonSetStart);

    g_hButtonSetEnd = CreateWindow(
        L"BUTTON", L"Set End",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        445, 375, 95, 25, // Placeholder
        hwnd, (HMENU)ID_BUTTON_SET_END,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonSetEnd);

    g_hEditStartTime = CreateWindow(
        L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        340, 405, 95, 20,
        hwnd, (HMENU)ID_EDIT_START_TIME,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hEditStartTime);

    g_hEditEndTime = CreateWindow(
        L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        445, 405, 95, 20,
        hwnd, (HMENU)ID_EDIT_END_TIME,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hEditEndTime);

    g_hLabelCutInfo = CreateWindow(
        L"STATIC", L"Cut points not set.",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 430, 200, 40, // Placeholder
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hLabelCutInfo);

    g_hButtonCut = CreateWindow(
        L"BUTTON", L"Cut & Save",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        340, 450, 200, 30, // Placeholder
        hwnd, (HMENU)ID_BUTTON_CUT,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hButtonCut);

   g_hCheckboxMergeAudio = CreateWindow(
       L"BUTTON", L"Merge Audios",
       WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
       340, 485, 200, 25, // Placeholder
       hwnd, (HMENU)ID_CHECKBOX_MERGE_AUDIO,
       (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
   SendMessage(g_hCheckboxMergeAudio, BM_SETCHECK, BST_UNCHECKED, 0);
   ApplyDarkTheme(g_hCheckboxMergeAudio);

    g_hRadioCopyCodec = CreateWindow(
        L"BUTTON", L"Copy Codec",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
        340, 515, 100, 20,
        hwnd, (HMENU)ID_RADIO_COPY_CODEC,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hRadioCopyCodec);
    SendMessage(g_hRadioCopyCodec, BM_SETCHECK, BST_CHECKED, 0);

    g_hRadioH264 = CreateWindow(
        L"BUTTON", L"Convert H264",
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
        445, 515, 100, 20,
        hwnd, (HMENU)ID_RADIO_H264,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hRadioH264);

    g_hEditBitrate = CreateWindow(
        L"EDIT", L"2000",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
        340, 540, 200, 20,
        hwnd, (HMENU)ID_EDIT_BITRATE,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hEditBitrate);


    // Disable controls until video is loaded
    EnableWindow(g_hButtonPlay, FALSE);
    EnableWindow(g_hButtonPause, FALSE);
    EnableWindow(g_hButtonStop, FALSE);
    EnableWindow(g_hTimeline, FALSE);
    EnableWindow(g_hListBoxAudioTracks, FALSE);
    EnableWindow(g_hButtonMuteTrack, FALSE);
    EnableWindow(g_hSliderTrackVolume, FALSE);
    EnableWindow(g_hSliderMasterVolume, FALSE);
    EnableWindow(g_hButtonSetStart, FALSE);
    EnableWindow(g_hButtonSetEnd, FALSE);
    EnableWindow(g_hEditStartTime, FALSE);
    EnableWindow(g_hEditEndTime, FALSE);
    EnableWindow(g_hButtonCut, FALSE);
    EnableWindow(g_hCheckboxMergeAudio, FALSE);
    EnableWindow(g_hRadioCopyCodec, FALSE);
    EnableWindow(g_hRadioH264, FALSE);
    EnableWindow(g_hEditBitrate, FALSE);
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
        LoadVideoFile(hwnd, std::wstring(szFile));
    }
}

void LoadVideoFile(HWND hwnd, const std::wstring& filename)
{
    if (g_videoPlayer && g_videoPlayer->LoadVideo(filename))
    {
        SetWindowTextW(g_hStatusText, (L"Loaded: " + filename).c_str());
        EnableWindow(g_hButtonPlay, TRUE);
        EnableWindow(g_hButtonPause, TRUE);
        EnableWindow(g_hButtonStop, TRUE);
        EnableWindow(g_hTimeline, TRUE);

        // Enable audio controls and populate audio tracks
        UpdateAudioTrackList();
        EnableWindow(g_hListBoxAudioTracks, TRUE);
        EnableWindow(g_hSliderMasterVolume, TRUE);
        if (g_videoPlayer->GetAudioTrackCount() > 0)
        {
            EnableWindow(g_hButtonMuteTrack, TRUE);
            EnableWindow(g_hSliderTrackVolume, TRUE);
            SendMessage(g_hListBoxAudioTracks, LB_SETCURSEL, 0, 0); // Select first track
            OnAudioTrackSelectionChanged();
        }

        // Enable editing controls and reset points
        EnableWindow(g_hButtonSetStart, TRUE);
        EnableWindow(g_hButtonSetEnd, TRUE);
        EnableWindow(g_hEditStartTime, TRUE);
        EnableWindow(g_hEditEndTime, TRUE);
        EnableWindow(g_hButtonCut, TRUE);
        EnableWindow(g_hCheckboxMergeAudio, TRUE);
        EnableWindow(g_hRadioCopyCodec, TRUE);
        EnableWindow(g_hRadioH264, TRUE);
        EnableWindow(g_hEditBitrate, TRUE);
        g_cutStartTime = -1.0;
        g_cutEndTime = -1.0;
        UpdateCutInfoLabel(hwnd);
        UpdateCutTimeEdits();

        UpdateControls();
        UpdateTimeline();
    }
    else
    {
        SetWindowTextW(g_hStatusText, L"Failed to load video file");
        MessageBoxW(hwnd, L"Failed to load the video file. Please check FFmpeg setup.", L"Error", MB_OK | MB_ICONERROR);
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
    EnableWindow(g_hTimeline, isLoaded);

    // Ensure audio controls remain enabled if a video is loaded
    EnableWindow(g_hListBoxAudioTracks, isLoaded && g_videoPlayer->GetAudioTrackCount() > 0);
    EnableWindow(g_hButtonMuteTrack, isLoaded && g_videoPlayer->GetAudioTrackCount() > 0);
    EnableWindow(g_hSliderTrackVolume, isLoaded && g_videoPlayer->GetAudioTrackCount() > 0);
    EnableWindow(g_hSliderMasterVolume, isLoaded);

    // Update editing controls
    EnableWindow(g_hButtonSetStart, isLoaded);
    EnableWindow(g_hButtonSetEnd, isLoaded);
    EnableWindow(g_hEditStartTime, isLoaded);
    EnableWindow(g_hEditEndTime, isLoaded);
    EnableWindow(g_hButtonCut, isLoaded && g_cutStartTime >= 0 && g_cutEndTime > g_cutStartTime);

   bool canMerge = g_videoPlayer && g_videoPlayer->GetAudioTrackCount() > 1;
   EnableWindow(g_hCheckboxMergeAudio, isLoaded && canMerge);
   EnableWindow(g_hRadioCopyCodec, isLoaded);
   EnableWindow(g_hRadioH264, isLoaded);
   EnableWindow(g_hEditBitrate, isLoaded && (SendMessage(g_hRadioH264, BM_GETCHECK, 0, 0) == BST_CHECKED));


    if (isLoaded)
    {
        double currentTime = g_videoPlayer->GetCurrentTime();
        double duration = g_videoPlayer->GetDuration();
        std::wstring currentTimeStr = FormatTime(currentTime);
        std::wstring durationStr = FormatTime(duration);
        wchar_t statusText[256];
        swprintf_s(statusText, _countof(statusText),
                   L"Time: %s / %s | Frame: %lld / %lld | %s",
                   currentTimeStr.c_str(), durationStr.c_str(),
                   g_videoPlayer->GetCurrentFrame(), g_videoPlayer->GetTotalFrames(),
                   isPlaying ? L"Playing" : L"Paused");
        SetWindowTextW(g_hStatusText, statusText);
    }
}

void UpdateTimeline()
{
    if (!g_videoPlayer || !g_videoPlayer->IsLoaded())
        return;
    double currentTime = g_videoPlayer->GetCurrentTime();
    double duration = g_videoPlayer->GetDuration();
    if (duration > 0)
    {
        InvalidateRect(g_hTimeline, NULL, FALSE);
    }
}

void UpdateAudioTrackList()
{
    if (!g_videoPlayer)
        return;
    
    // Clear existing items
    SendMessage(g_hListBoxAudioTracks, LB_RESETCONTENT, 0, 0);
    
    // Add audio tracks
    int trackCount = g_videoPlayer->GetAudioTrackCount();
    for (int i = 0; i < trackCount; i++)
    {
        std::string trackName = g_videoPlayer->GetAudioTrackName(i);
        std::wstring wTrackName(trackName.begin(), trackName.end());
        
        // Add mute status to the display name
        if (g_videoPlayer->IsAudioTrackMuted(i))
            wTrackName += L" (MUTED)";
        
        SendMessage(g_hListBoxAudioTracks, LB_ADDSTRING, 0, (LPARAM)wTrackName.c_str());
    }
}

void OnAudioTrackSelectionChanged()
{
    if (!g_videoPlayer)
        return;
    
    int selectedIndex = (int)SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
    if (selectedIndex != LB_ERR)
    {
        // Update track volume slider
        float volume = g_videoPlayer->GetAudioTrackVolume(selectedIndex);
        int sliderPos = (int)(volume * 100); // Convert to 0-200 range
        SendMessage(g_hSliderTrackVolume, TBM_SETPOS, TRUE, sliderPos);
        
        // Update mute button text
        bool isMuted = g_videoPlayer->IsAudioTrackMuted(selectedIndex);
        SetWindowTextW(g_hButtonMuteTrack, isMuted ? L"Unmute" : L"Mute");
    }
}

void OnMuteTrackClicked()
{
    if (!g_videoPlayer)
        return;
    
    int selectedIndex = (int)SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
    if (selectedIndex != LB_ERR)
    {
        bool currentlyMuted = g_videoPlayer->IsAudioTrackMuted(selectedIndex);
        g_videoPlayer->SetAudioTrackMuted(selectedIndex, !currentlyMuted);
        
        // Update the display
        UpdateAudioTrackList();
        SendMessage(g_hListBoxAudioTracks, LB_SETCURSEL, selectedIndex, 0);
        OnAudioTrackSelectionChanged();
    }
}

void OnTrackVolumeChanged()
{
    if (!g_videoPlayer)
        return;
    
    int selectedIndex = (int)SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
    if (selectedIndex != LB_ERR)
    {
        int sliderPos = (int)SendMessage(g_hSliderTrackVolume, TBM_GETPOS, 0, 0);
        float volume = sliderPos / 100.0f; // Convert from 0-200 to 0.0-2.0
        g_videoPlayer->SetAudioTrackVolume(selectedIndex, volume);
    }
}

void OnMasterVolumeChanged()
{
    if (!g_videoPlayer)
        return;
    
    int sliderPos = (int)SendMessage(g_hSliderMasterVolume, TBM_GETPOS, 0, 0);
    float volume = sliderPos / 100.0f; // Convert from 0-200 to 0.0-2.0
    g_videoPlayer->SetMasterVolume(volume);
    
    // Update the track volume slider if a track is selected
    int selectedIndex = (int)SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
    if (selectedIndex != LB_ERR)
    {
        OnAudioTrackSelectionChanged();
    }
}

std::wstring FormatTime(double totalSeconds, bool showMilliseconds)
{
    if (totalSeconds < 0) totalSeconds = 0;
    int hours = static_cast<int>(totalSeconds) / 3600;
    int minutes = (static_cast<int>(totalSeconds) % 3600) / 60;
    int seconds = static_cast<int>(totalSeconds) % 60;

    wchar_t buffer[64];
    if (showMilliseconds)
    {
        int milliseconds = static_cast<int>((totalSeconds - static_cast<int>(totalSeconds)) * 100);
        if (hours > 0)
            swprintf_s(buffer, _countof(buffer), L"%d:%02d:%02d.%02d", hours, minutes, seconds, milliseconds);
        else
            swprintf_s(buffer, _countof(buffer), L"%02d:%02d.%02d", minutes, seconds, milliseconds);
    }
    else
    {
        if (hours > 0)
            swprintf_s(buffer, _countof(buffer), L"%d:%02d:%02d", hours, minutes, seconds);
        else
            swprintf_s(buffer, _countof(buffer), L"%02d:%02d", minutes, seconds);
    }
    return std::wstring(buffer);
}

double ParseTimeString(const std::wstring& str)
{
    int h = 0, m = 0;
    double s = 0.0;
    if (swscanf(str.c_str(), L"%d:%d:%lf", &h, &m, &s) == 3)
        return h * 3600 + m * 60 + s;
    if (swscanf(str.c_str(), L"%d:%lf", &m, &s) == 2)
        return m * 60 + s;
    return -1.0;
}

void UpdateCutTimeEdits()
{
    if (g_hEditStartTime)
    {
        if (g_cutStartTime >= 0)
            SetWindowTextW(g_hEditStartTime, FormatTime(g_cutStartTime, true).c_str());
        else
            SetWindowTextW(g_hEditStartTime, L"");
    }
    if (g_hEditEndTime)
    {
        if (g_cutEndTime >= 0)
            SetWindowTextW(g_hEditEndTime, FormatTime(g_cutEndTime, true).c_str());
        else
            SetWindowTextW(g_hEditEndTime, L"");
    }
}

void UpdateCutInfoLabel(HWND hwnd)
{
    wchar_t buffer[128];
    if (g_cutStartTime < 0 && g_cutEndTime < 0)
    {
        swprintf_s(buffer, L"Cut points not set.");
    }
    else
    {
        wchar_t startStr[64], endStr[64];
        if (g_cutStartTime >= 0) {
            std::wstring timeStr = FormatTime(g_cutStartTime, true);
            swprintf_s(startStr, _countof(startStr), L"Start: %s", timeStr.c_str());
        } else {
            swprintf_s(startStr, _countof(startStr), L"Start: Not set");
        }

        if (g_cutEndTime >= 0) {
            std::wstring timeStr = FormatTime(g_cutEndTime, true);
            swprintf_s(endStr, _countof(endStr), L"End: %s", timeStr.c_str());
        } else {
            swprintf_s(endStr, _countof(endStr), L"End: Not set");
        }

        swprintf_s(buffer, _countof(buffer), L"%s\n%s", startStr, endStr);
    }
    SetWindowTextW(g_hLabelCutInfo, buffer);
    // Also update the cut button state
    UpdateControls();
    UpdateCutTimeEdits();
}

void OnSetStartClicked(HWND hwnd)
{
    if (!g_videoPlayer || !g_videoPlayer->IsLoaded()) return;
    g_cutStartTime = g_videoPlayer->GetCurrentTime();
    if (g_cutEndTime >= 0 && g_cutStartTime >= g_cutEndTime)
    {
        g_cutEndTime = -1.0; // Invalidate end time if it's before new start time
    }
    UpdateCutInfoLabel(hwnd);
    UpdateCutTimeEdits();
}

void OnSetEndClicked(HWND hwnd)
{
    if (!g_videoPlayer || !g_videoPlayer->IsLoaded()) return;
    double currentTime = g_videoPlayer->GetCurrentTime();
    if (g_cutStartTime >= 0 && currentTime <= g_cutStartTime)
    {
        MessageBoxW(hwnd, L"End point must be after the start point.", L"Invalid Time", MB_OK | MB_ICONWARNING);
        return;
    }
    g_cutEndTime = currentTime;
    UpdateCutInfoLabel(hwnd);
    UpdateCutTimeEdits();
}

void OnCutClicked(HWND hwnd)
{
    if (!g_videoPlayer || g_cutStartTime < 0 || g_cutEndTime <= g_cutStartTime)
    {
        MessageBoxW(hwnd, L"Please set valid start and end points for the cut.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"MP4 Video\0*.mp4\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"mp4";

    if (GetSaveFileNameW(&ofn))
    {
        SetWindowTextW(g_hStatusText, L"Cutting video... Please wait.");
        EnableWindow(hwnd, FALSE); // Disable UI during cut

        bool mergeAudio = IsDlgButtonChecked(hwnd, ID_CHECKBOX_MERGE_AUDIO) == BST_CHECKED;
        bool convertH264 = SendMessage(g_hRadioH264, BM_GETCHECK, 0, 0) == BST_CHECKED;
        wchar_t bitrateText[32];
        GetWindowTextW(g_hEditBitrate, bitrateText, 32);
        int bitrate = _wtoi(bitrateText);

        ShowProgressWindow(hwnd);
        bool success = g_videoPlayer->CutVideo(std::wstring(szFile), g_cutStartTime, g_cutEndTime,
                                              mergeAudio, convertH264, bitrate, g_hProgressBar);
        CloseProgressWindow();

        EnableWindow(hwnd, TRUE); // Re-enable UI

        MessageBoxW(hwnd, success ? L"Video successfully cut and saved." : L"Failed to cut the video.",
                    success ? L"Success" : L"Error", MB_OK | (success ? MB_ICONINFORMATION : MB_ICONERROR));
        UpdateControls();
    }
}

void RepositionControls(HWND hwnd)
{
    if (!g_videoPlayer) return;

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    // Main controls
    int mainControlsY = 10;
    int mainControlsHeight = 30;
    MoveWindow(g_hButtonOpen, 10, mainControlsY, 100, mainControlsHeight, TRUE);
    MoveWindow(g_hButtonPlay, 120, mainControlsY, 60, mainControlsHeight, TRUE);
    MoveWindow(g_hButtonPause, 190, mainControlsY, 60, mainControlsHeight, TRUE);
    MoveWindow(g_hButtonStop, 260, mainControlsY, 60, mainControlsHeight, TRUE);

    // Audio controls (aligned to the right)
    int audioControlsWidth = 220;
    int audioControlsX = clientRect.right - audioControlsWidth - 10;
    int audioControlsY = 50;

    MoveWindow(g_hLabelAudioTracks, audioControlsX, audioControlsY, 200, 20, TRUE);
    MoveWindow(g_hListBoxAudioTracks, audioControlsX, audioControlsY + 25, 200, 100, TRUE);
    MoveWindow(g_hButtonMuteTrack, audioControlsX, audioControlsY + 135, 100, 25, TRUE);

    MoveWindow(g_hLabelTrackVolume, audioControlsX, audioControlsY + 170, 200, 20, TRUE);
    MoveWindow(g_hSliderTrackVolume, audioControlsX, audioControlsY + 195, 200, 30, TRUE);

    MoveWindow(g_hLabelMasterVolume, audioControlsX, audioControlsY + 235, 200, 20, TRUE);
    MoveWindow(g_hSliderMasterVolume, audioControlsX, audioControlsY + 260, 200, 30, TRUE);

    // Editing controls (below audio)
    int editingControlsY = audioControlsY + 260 + 40;
    MoveWindow(g_hLabelEditing, audioControlsX, editingControlsY, 200, 20, TRUE);
    MoveWindow(g_hButtonSetStart, audioControlsX, editingControlsY + 25, 95, 25, TRUE);
    MoveWindow(g_hButtonSetEnd, audioControlsX + 105, editingControlsY + 25, 95, 25, TRUE);
    MoveWindow(g_hEditStartTime, audioControlsX, editingControlsY + 55, 95, 20, TRUE);
    MoveWindow(g_hEditEndTime, audioControlsX + 105, editingControlsY + 55, 95, 20, TRUE);
    MoveWindow(g_hLabelCutInfo, audioControlsX, editingControlsY + 80, 200, 40, TRUE);
    MoveWindow(g_hButtonCut, audioControlsX, editingControlsY + 125, 200, 30, TRUE);
    MoveWindow(g_hCheckboxMergeAudio, audioControlsX, editingControlsY + 160, 200, 25, TRUE);
    MoveWindow(g_hRadioCopyCodec, audioControlsX, editingControlsY + 190, 100, 20, TRUE);
    MoveWindow(g_hRadioH264, audioControlsX + 105, editingControlsY + 190, 100, 20, TRUE);
    MoveWindow(g_hEditBitrate, audioControlsX, editingControlsY + 215, 200, 20, TRUE);

    // Video area (takes up remaining space)
    int videoSectionWidth = clientRect.right - audioControlsWidth - 30;
    int bottomControlsHeight = 100;
    g_videoPlayer->SetPosition(
        10,
        mainControlsY + mainControlsHeight + 10,
        videoSectionWidth,
        clientRect.bottom - (mainControlsY + mainControlsHeight + 10) - bottomControlsHeight
    );

    // Bottom controls
    int bottomControlsY = clientRect.bottom - 90;
    MoveWindow(g_hTimeline, 10, bottomControlsY, videoSectionWidth, 30, TRUE);
    MoveWindow(g_hStatusText, 10, bottomControlsY + 40, videoSectionWidth, 20, TRUE);

    // Redraw all controls
    InvalidateRect(hwnd, NULL, TRUE);
}

void ShowProgressWindow(HWND parent)
{
    INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&ic);
    g_hProgressWnd = CreateWindowEx(WS_EX_TOPMOST, L"#32770", L"Exporting", WS_CAPTION | WS_POPUPWINDOW,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 300, 100,
                                   parent, nullptr, (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    g_hProgressBar = CreateWindowEx(0, PROGRESS_CLASS, nullptr,
                                   WS_CHILD | WS_VISIBLE, 20, 30, 260, 20,
                                   g_hProgressWnd, nullptr, (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    SendMessage(g_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    ShowWindow(g_hProgressWnd, SW_SHOW);
    UpdateWindow(g_hProgressWnd);
}

void UpdateProgress(int percent)
{
    if (g_hProgressBar)
        SendMessage(g_hProgressBar, PBM_SETPOS, percent, 0);
}

void CloseProgressWindow()
{
    if (g_hProgressWnd)
    {
        DestroyWindow(g_hProgressWnd);
        g_hProgressWnd = nullptr;
        g_hProgressBar = nullptr;
    }
}

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
void ApplyDarkTheme(HWND hwnd)
{
    if (g_hFont)
        SendMessage(hwnd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
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
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    RegisterClass(&wc);

    WNDCLASS twc = {};
    twc.lpfnWndProc = TimelineProc;
    twc.hInstance = hInstance;
    twc.lpszClassName = L"TimelineClass";
    twc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    twc.hbrBackground = nullptr; // custom paint
    RegisterClass(&twc);

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
                UpdateControls();
                UpdateTimeline();
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
