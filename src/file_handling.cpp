#include "file_handling.h"
#include "video_player.h"
#include "ui_updates.h"
#include "editing.h"
#include <commdlg.h>

// Forward declarations
void UpdateAudioTrackList();
void OnAudioTrackSelectionChanged();
void UpdateCutInfoLabel(HWND hwnd);
void UpdateCutTimeEdits();
void UpdateControls();
void UpdateTimeline();

// Global variables
extern VideoPlayer *g_videoPlayer;
extern HWND g_hStatusText, g_hButtonPlay, g_hButtonPause, g_hButtonStop, g_hTimeline, g_hListBoxAudioTracks, g_hButtonMuteTrack, g_hSliderTrackVolume, g_hSliderMasterVolume, g_hButtonSetStart, g_hButtonSetEnd, g_hEditStartTime, g_hEditEndTime, g_hButtonCut, g_hCheckboxMergeAudio, g_hRadioCopyCodec, g_hRadioH264, g_hEditBitrate, g_hEditTargetSize, g_hLabelTargetSize;
extern double g_cutStartTime, g_cutEndTime;

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
        EnableWindow(g_hEditTargetSize, TRUE);
        EnableWindow(g_hLabelTargetSize, TRUE);
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
