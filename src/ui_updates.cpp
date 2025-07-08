#include "ui_updates.h"
#include "video_player.h"
#include "utils.h"
#include <string>
#include <commctrl.h>

// Forward declarations
std::wstring FormatTime(double totalSeconds, bool showMilliseconds);

// Global variables
extern VideoPlayer *g_videoPlayer;
extern HWND g_hButtonPlay, g_hButtonPause, g_hButtonStop, g_hTimeline, g_hListBoxAudioTracks, g_hButtonMuteTrack, g_hSliderTrackVolume, g_hSliderMasterVolume, g_hButtonSetStart, g_hButtonSetEnd, g_hEditStartTime, g_hEditEndTime, g_hButtonCut, g_hCheckboxMergeAudio, g_hRadioCopyCodec, g_hRadioH264, g_hEditBitrate, g_hEditTargetSize, g_hStatusText, g_hLabelCutInfo, g_hRadioUseBitrate, g_hRadioUseSize, g_hLabelBitrate, g_hLabelTargetSize;
extern double g_cutStartTime, g_cutEndTime;

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
    bool hasStart = g_cutStartTime >= 0;
    bool hasEnd = g_cutEndTime >= 0;
    if (!hasStart && !hasEnd)
    {
        SetWindowTextW(g_hButtonCut, L"Export Video");
        EnableWindow(g_hButtonCut, isLoaded);
    }
    else
    {
        SetWindowTextW(g_hButtonCut, L"Cut Video");
        EnableWindow(g_hButtonCut, isLoaded && hasStart && hasEnd && g_cutEndTime > g_cutStartTime);
    }

   bool canMerge = g_videoPlayer && g_videoPlayer->GetAudioTrackCount() > 1;
   EnableWindow(g_hCheckboxMergeAudio, isLoaded && canMerge);
   EnableWindow(g_hRadioCopyCodec, isLoaded);
   EnableWindow(g_hRadioH264, isLoaded);

   bool convertH264 = SendMessage(g_hRadioH264, BM_GETCHECK, 0, 0) == BST_CHECKED;
   bool useBitrate = SendMessage(g_hRadioUseBitrate, BM_GETCHECK, 0, 0) == BST_CHECKED;

   EnableWindow(g_hRadioUseBitrate, isLoaded && convertH264);
   EnableWindow(g_hRadioUseSize, isLoaded && convertH264);
   ShowWindow(g_hRadioUseBitrate, convertH264 ? SW_SHOW : SW_HIDE);
   ShowWindow(g_hRadioUseSize, convertH264 ? SW_SHOW : SW_HIDE);

   EnableWindow(g_hEditBitrate, isLoaded && convertH264 && useBitrate);
   ShowWindow(g_hLabelBitrate, convertH264 && useBitrate ? SW_SHOW : SW_HIDE);
   ShowWindow(g_hEditBitrate, convertH264 && useBitrate ? SW_SHOW : SW_HIDE);

   EnableWindow(g_hEditTargetSize, isLoaded && convertH264 && !useBitrate);
   ShowWindow(g_hLabelTargetSize, convertH264 && !useBitrate ? SW_SHOW : SW_HIDE);
   ShowWindow(g_hEditTargetSize, convertH264 && !useBitrate ? SW_SHOW : SW_HIDE);


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
