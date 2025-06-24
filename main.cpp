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
#define ID_LISTBOX_AUDIO_TRACKS 1007
#define ID_BUTTON_MUTE_TRACK 1008
#define ID_SLIDER_TRACK_VOLUME 1009
#define ID_SLIDER_MASTER_VOLUME 1010

// Editing Control IDs
#define ID_BUTTON_SET_START 1011
#define ID_BUTTON_SET_END 1012
#define ID_BUTTON_CUT 1013
// Global variables
VideoPlayer *g_videoPlayer = nullptr;
HWND g_hButtonOpen, g_hButtonPlay, g_hButtonPause, g_hButtonStop;
HWND g_hSliderSeek;
HWND g_hStatusText;
HWND g_hListBoxAudioTracks, g_hButtonMuteTrack;
HWND g_hSliderTrackVolume, g_hSliderMasterVolume;
HWND g_hLabelAudioTracks, g_hLabelTrackVolume, g_hLabelMasterVolume, g_hLabelEditing;
HWND g_hButtonSetStart, g_hButtonSetEnd, g_hButtonCut;
HWND g_hLabelCutInfo;
double g_cutStartTime = -1.0;
double g_cutEndTime = -1.0;

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void OpenVideoFile(HWND hwnd);
void UpdateControls();
void UpdateSeekBar();
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
void RepositionControls(HWND hwnd);


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
        }
        
        // Handle listbox selection changes
        if (HIWORD(wParam) == LBN_SELCHANGE && (HWND)lParam == g_hListBoxAudioTracks)
        {
            OnAudioTrackSelectionChanged();
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
        else if ((HWND)lParam == g_hSliderTrackVolume)
        {
            OnTrackVolumeChanged();
        }
        else if ((HWND)lParam == g_hSliderMasterVolume)
        {
            OnMasterVolumeChanged();
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

    // Seek slider
    g_hSliderSeek = CreateWindow(
        TRACKBAR_CLASS, L"Seek",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_BOTH,
        10, 370, 600, 30, // Placeholder position
        hwnd, (HMENU)ID_SLIDER_SEEK,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    SendMessage(g_hSliderSeek, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessage(g_hSliderSeek, TBM_SETPOS, TRUE, 0);

    // Status text
    g_hStatusText = CreateWindow(
        L"STATIC", L"No video loaded",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, 410, 600, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Audio controls section
    // Audio tracks label
    g_hLabelAudioTracks = CreateWindow(
        L"STATIC", L"Audio Tracks:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 50, 100, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Audio tracks listbox
    g_hListBoxAudioTracks = CreateWindow(
        L"LISTBOX", nullptr,
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        340, 75, 200, 100, // Placeholder position
        hwnd, (HMENU)ID_LISTBOX_AUDIO_TRACKS,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Mute track button
    g_hButtonMuteTrack = CreateWindow(
        L"BUTTON", L"Mute/Unmute",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        340, 185, 100, 25, // Placeholder position
        hwnd, (HMENU)ID_BUTTON_MUTE_TRACK,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Track volume label
    g_hLabelTrackVolume = CreateWindow(
        L"STATIC", L"Track Volume:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 220, 100, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Track volume slider
    g_hSliderTrackVolume = CreateWindow(
        TRACKBAR_CLASS, L"Track Volume",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_BOTH,
        340, 245, 200, 30, // Placeholder position
        hwnd, (HMENU)ID_SLIDER_TRACK_VOLUME,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    SendMessage(g_hSliderTrackVolume, TBM_SETRANGE, TRUE, MAKELONG(0, 200)); // 0-200% volume
    SendMessage(g_hSliderTrackVolume, TBM_SETPOS, TRUE, 100); // Default 100%

    // Master volume label
    g_hLabelMasterVolume = CreateWindow(
        L"STATIC", L"Master Volume:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 285, 100, 20, // Placeholder position
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Master volume slider
    g_hSliderMasterVolume = CreateWindow(
        TRACKBAR_CLASS, L"Master Volume",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_BOTH,
        340, 310, 200, 30, // Placeholder position
        hwnd, (HMENU)ID_SLIDER_MASTER_VOLUME,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    SendMessage(g_hSliderMasterVolume, TBM_SETRANGE, TRUE, MAKELONG(0, 200)); // 0-200% volume
    SendMessage(g_hSliderMasterVolume, TBM_SETPOS, TRUE, 100); // Default 100%

    // Editing controls section
    g_hLabelEditing = CreateWindow(
        L"STATIC", L"Editing:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 350, 100, 20, // Placeholder
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    g_hButtonSetStart = CreateWindow(
        L"BUTTON", L"Set Start",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        340, 375, 95, 25, // Placeholder
        hwnd, (HMENU)ID_BUTTON_SET_START,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    g_hButtonSetEnd = CreateWindow(
        L"BUTTON", L"Set End",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        445, 375, 95, 25, // Placeholder
        hwnd, (HMENU)ID_BUTTON_SET_END,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    g_hLabelCutInfo = CreateWindow(
        L"STATIC", L"Cut points not set.",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        340, 405, 200, 40, // Placeholder
        hwnd, nullptr,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    g_hButtonCut = CreateWindow(
        L"BUTTON", L"Cut & Save",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        340, 450, 200, 30, // Placeholder
        hwnd, (HMENU)ID_BUTTON_CUT,
        (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

    // Disable controls until video is loaded
    EnableWindow(g_hButtonPlay, FALSE);
    EnableWindow(g_hButtonPause, FALSE);
    EnableWindow(g_hButtonStop, FALSE);
    EnableWindow(g_hSliderSeek, FALSE);
    EnableWindow(g_hListBoxAudioTracks, FALSE);
    EnableWindow(g_hButtonMuteTrack, FALSE);
    EnableWindow(g_hSliderTrackVolume, FALSE);
    EnableWindow(g_hSliderMasterVolume, FALSE);
    EnableWindow(g_hButtonSetStart, FALSE);
    EnableWindow(g_hButtonSetEnd, FALSE);
    EnableWindow(g_hButtonCut, FALSE);
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
            EnableWindow(g_hButtonCut, TRUE);
            g_cutStartTime = -1.0;
            g_cutEndTime = -1.0;
            UpdateCutInfoLabel(hwnd);
            
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

    // Ensure audio controls remain enabled if a video is loaded
    EnableWindow(g_hListBoxAudioTracks, isLoaded && g_videoPlayer->GetAudioTrackCount() > 0);
    EnableWindow(g_hButtonMuteTrack, isLoaded && g_videoPlayer->GetAudioTrackCount() > 0);
    EnableWindow(g_hSliderTrackVolume, isLoaded && g_videoPlayer->GetAudioTrackCount() > 0);
    EnableWindow(g_hSliderMasterVolume, isLoaded);

    // Update editing controls
    EnableWindow(g_hButtonSetStart, isLoaded);
    EnableWindow(g_hButtonSetEnd, isLoaded);
    EnableWindow(g_hButtonCut, isLoaded && g_cutStartTime >= 0 && g_cutEndTime > g_cutStartTime);

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
    
    int selectedIndex = SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
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
    
    int selectedIndex = SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
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
    
    int selectedIndex = SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
    if (selectedIndex != LB_ERR)
    {
        int sliderPos = SendMessage(g_hSliderTrackVolume, TBM_GETPOS, 0, 0);
        float volume = sliderPos / 100.0f; // Convert from 0-200 to 0.0-2.0
        g_videoPlayer->SetAudioTrackVolume(selectedIndex, volume);
    }
}

void OnMasterVolumeChanged()
{
    if (!g_videoPlayer)
        return;
    
    int sliderPos = SendMessage(g_hSliderMasterVolume, TBM_GETPOS, 0, 0);
    float volume = sliderPos / 100.0f; // Convert from 0-200 to 0.0-2.0
    g_videoPlayer->SetMasterVolume(volume);
    
    // Update the track volume slider if a track is selected
    int selectedIndex = SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
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

        bool success = g_videoPlayer->CutVideo(std::wstring(szFile), g_cutStartTime, g_cutEndTime);

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
    MoveWindow(g_hLabelCutInfo, audioControlsX, editingControlsY + 55, 200, 40, TRUE);
    MoveWindow(g_hButtonCut, audioControlsX, editingControlsY + 100, 200, 30, TRUE);

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
    MoveWindow(g_hSliderSeek, 10, bottomControlsY, videoSectionWidth, 30, TRUE);
    MoveWindow(g_hStatusText, 10, bottomControlsY + 40, videoSectionWidth, 20, TRUE);

    // Redraw all controls
    InvalidateRect(hwnd, NULL, TRUE);
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
