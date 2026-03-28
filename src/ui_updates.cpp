#include "ui_updates.h"
#include "video_player.h"
#include "utils.h"
#include <string>
#include <commctrl.h>
#include <cmath>

// Forward declarations
std::wstring FormatTime(double totalSeconds, bool showMilliseconds);

// Global variables
extern VideoPlayer *g_videoPlayer;
extern HWND g_hButtonPlay, g_hButtonPause, g_hButtonStop, g_hTimeline, g_hListBoxAudioTracks, g_hButtonMuteTrack, g_hSliderTrackVolume, g_hSliderMasterVolume, g_hButtonSetStart, g_hButtonSetEnd, g_hEditStartTime, g_hEditEndTime, g_hButtonPlayClip, g_hButtonPlayEnd, g_hButtonCut, g_hCheckboxMergeAudio, g_hRadioCopyCodec, g_hRadioH264, g_hEditBitrate, g_hEditTargetSize, g_hStatusText, g_hLabelCutInfo, g_hRadioUseBitrate, g_hRadioUseSize, g_hLabelBitrate, g_hLabelTargetSize, g_hLabelTrackVolume, g_hLabelMasterVolume;
extern double g_cutStartTime, g_cutEndTime;
extern double g_previewSeekTime;
extern bool g_isPanelVisible;

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
    EnableWindow(g_hButtonPlayClip, isLoaded && hasStart && hasEnd && g_cutEndTime > g_cutStartTime);
    EnableWindow(g_hButtonPlayEnd, isLoaded && hasStart && hasEnd && g_cutEndTime > g_cutStartTime);

   bool canMerge = g_videoPlayer && g_videoPlayer->GetAudioTrackCount() > 1;
   EnableWindow(g_hCheckboxMergeAudio, isLoaded && canMerge);

   bool anyCrop = g_videoPlayer && g_videoPlayer->HasAnyCrop();
   bool hasCrop = g_videoPlayer && g_videoPlayer->hasCrop;
   EnableWindow(g_hRadioCopyCodec, isLoaded && !anyCrop);
   EnableWindow(g_hRadioH264, isLoaded);
   if (anyCrop) {
       SendMessage(g_hRadioH264, BM_SETCHECK, BST_CHECKED, 0);
       SendMessage(g_hRadioCopyCodec, BM_SETCHECK, BST_UNCHECKED, 0);
   }

   bool convertH264 = SendMessage(g_hRadioH264, BM_GETCHECK, 0, 0) == BST_CHECKED;
   bool useBitrate = SendMessage(g_hRadioUseBitrate, BM_GETCHECK, 0, 0) == BST_CHECKED;

   EnableWindow(g_hRadioUseBitrate, isLoaded && convertH264);
   EnableWindow(g_hRadioUseSize, isLoaded && convertH264);
   ShowWindow(g_hRadioUseBitrate, (convertH264 && g_isPanelVisible) ? SW_SHOW : SW_HIDE);
   ShowWindow(g_hRadioUseSize, (convertH264 && g_isPanelVisible) ? SW_SHOW : SW_HIDE);

   EnableWindow(g_hEditBitrate, isLoaded && convertH264 && useBitrate);
   ShowWindow(g_hLabelBitrate, (convertH264 && useBitrate && g_isPanelVisible) ? SW_SHOW : SW_HIDE);
   ShowWindow(g_hEditBitrate, (convertH264 && useBitrate && g_isPanelVisible) ? SW_SHOW : SW_HIDE);

   EnableWindow(g_hEditTargetSize, isLoaded && convertH264 && !useBitrate);
   ShowWindow(g_hLabelTargetSize, (convertH264 && !useBitrate && g_isPanelVisible) ? SW_SHOW : SW_HIDE);
   ShowWindow(g_hEditTargetSize, (convertH264 && !useBitrate && g_isPanelVisible) ? SW_SHOW : SW_HIDE);


    if (isLoaded)
    {
        double currentTime;
        int64_t currentFrame;
        
        if (g_previewSeekTime >= 0.0) {
            currentTime = g_previewSeekTime;
            // Approximate frame number from time
            if (g_videoPlayer->frameRate > 0)
                currentFrame = (int64_t)(currentTime * g_videoPlayer->frameRate);
            else
                currentFrame = g_videoPlayer->GetCurrentFrame();
        } else {
            currentTime = g_videoPlayer->GetCurrentTime();
            currentFrame = g_videoPlayer->GetCurrentFrame();
        }

        double duration = g_videoPlayer->GetDuration();
        std::wstring currentTimeStr = FormatTime(currentTime);
        std::wstring durationStr = FormatTime(duration);
        wchar_t statusText[256];
        swprintf_s(statusText, _countof(statusText),
                   L"Time: %s / %s | Frame: %lld / %lld | %s",
                   currentTimeStr.c_str(), durationStr.c_str(),
                   currentFrame, g_videoPlayer->GetTotalFrames(),
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
        
        // Add status indicators to the display name
        std::wstring status = L"";
        if (g_videoPlayer->IsAudioTrackMuted(i))
            status += L" (MUTED)";
        if (g_videoPlayer->IsVoiceIsolationEnabled(i))
            status += L" (VOICE)";
        
        wTrackName += status;
        
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
        
        // Slider layout:
        // 0 => hard mute (absolute 0 amplitude)
        // 1..601 => -30.0dB..+30.0dB in 0.1dB steps, with 301 = 0.0dB
        int sliderPos = 301;
        if (volume <= 0.0f) {
            sliderPos = 0;
        } else {
            float db = 20.0f * log10f(volume);
            if (db < -30.0f) db = -30.0f;
            if (db > 30.0f) db = 30.0f;
            sliderPos = static_cast<int>(301.0f + (db * 10.0f));
            if (sliderPos < 1) sliderPos = 1;
            if (sliderPos > 601) sliderPos = 601;
        }

        SendMessage(g_hSliderTrackVolume, TBM_SETPOS, TRUE, sliderPos);
        
        // Update label with dB value
        wchar_t buf[64];
        if (sliderPos == 0) {
            swprintf_s(buf, L"Track Volume: Mute");
        } else {
            float dbDisplay = (sliderPos - 301.0f) / 10.0f;
            if (dbDisplay > 0.05f)
                swprintf_s(buf, L"Track Volume: +%.1f dB", dbDisplay);
            else if (dbDisplay < -0.05f)
                swprintf_s(buf, L"Track Volume: %.1f dB", dbDisplay);
            else
                swprintf_s(buf, L"Track Volume: 0.0 dB");
        }
        SetWindowTextW(g_hLabelTrackVolume, buf);

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
        
        // Magnetic snap to center (0dB, slider 301)
        if (sliderPos > 299 && sliderPos < 303) {
            sliderPos = 301;
            SendMessage(g_hSliderTrackVolume, TBM_SETPOS, TRUE, sliderPos);
        }

        // Convert slider position to dB then to volume (amplitude)
        // 0 => hard mute
        // 1..601 => -30dB..+30dB
        float db = 0.0f;
        float volume = 0.0f;
        if (sliderPos > 0)
        {
            db = (sliderPos - 301.0f) / 10.0f;
            volume = powf(10.0f, db / 20.0f);
        }

        g_videoPlayer->SetAudioTrackVolume(selectedIndex, volume);

        // Update label with dB value
        wchar_t buf[64];
        if (sliderPos == 0)
            swprintf_s(buf, L"Track Volume: Mute");
        else if (db > 0.05f)
            swprintf_s(buf, L"Track Volume: +%.1f dB", db);
        else if (db < -0.05f)
            swprintf_s(buf, L"Track Volume: %.1f dB", db);
        else
            swprintf_s(buf, L"Track Volume: 0.0 dB");
        SetWindowTextW(g_hLabelTrackVolume, buf);
    }
}

void OnMasterVolumeChanged()
{
    if (!g_videoPlayer)
        return;
    
    int sliderPos = (int)SendMessage(g_hSliderMasterVolume, TBM_GETPOS, 0, 0);
    
    // Magnetic snap to center (0dB, slider 301)
    if (sliderPos > 299 && sliderPos < 303) {
        sliderPos = 301;
        SendMessage(g_hSliderMasterVolume, TBM_SETPOS, TRUE, sliderPos);
    }

    // Convert slider position to dB then to volume (amplitude)
    // 0 => hard mute
    // 1..601 => -30dB..+30dB
    float db = 0.0f;
    float volume = 0.0f;
    if (sliderPos > 0)
    {
        db = (sliderPos - 301.0f) / 10.0f;
        volume = powf(10.0f, db / 20.0f);
    }

    g_videoPlayer->SetMasterVolume(volume);
    
    // Update label with dB value
    wchar_t buf[64];
    if (sliderPos == 0)
        swprintf_s(buf, L"Master Volume: Mute");
    else if (db > 0.05f)
        swprintf_s(buf, L"Master Volume: +%.1f dB", db);
    else if (db < -0.05f)
        swprintf_s(buf, L"Master Volume: %.1f dB", db);
    else
        swprintf_s(buf, L"Master Volume: 0.0 dB");
    SetWindowTextW(g_hLabelMasterVolume, buf);
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
