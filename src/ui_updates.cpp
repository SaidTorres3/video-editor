#include "ui_updates.h"
#include "video_player.h"
#include "utils.h"
#include "editing.h"
#include "options_window.h"
#include "timeline.h"
#include <string>
#include <commctrl.h>
#include <cmath>
#include <atomic>

// Forward declarations
std::wstring FormatTime(double totalSeconds, bool showMilliseconds);

// Global variables
extern VideoPlayer *g_videoPlayer;
extern HWND g_hButtonOpen;
extern HWND g_hButtonPlay, g_hButtonPause, g_hButtonStop, g_hTimeline, g_hListBoxAudioTracks, g_hButtonMuteTrack, g_hSliderTrackVolume, g_hSliderMasterVolume, g_hButtonSetStart, g_hButtonSetEnd, g_hEditStartTime, g_hEditEndTime, g_hButtonPlayClip, g_hButtonPlayEnd, g_hButtonAddClip, g_hButtonClearClips, g_hListBoxCutSegments, g_hButtonUpdateClip, g_hButtonRemoveClip, g_hButtonPlayAllClips, g_hButtonCut, g_hCheckboxMergeAudio, g_hRadioCopyCodec, g_hRadioH264, g_hEditBitrate, g_hEditTargetSize, g_hStatusText, g_hLabelCutInfo, g_hRadioUseBitrate, g_hRadioUseSize, g_hLabelBitrate, g_hLabelTargetSize, g_hLabelTrackVolume, g_hLabelMasterVolume;
extern HWND g_hButtonSpeedDown, g_hButtonSpeedUp, g_hEditPlaybackSpeed;
extern double g_cutStartTime, g_cutEndTime;
extern double g_previewSeekTime;
extern bool g_isPanelVisible;

extern bool g_resumePlayAfterSeek;

namespace {
std::atomic<VideoPlayer*> g_pendingPreviewReleasePlayer{nullptr};
std::atomic<uint64_t> g_previewReleaseAfterPresentation{0};
}

void FinalizePlayingUiSeek(VideoPlayer* player, double seconds)
{
    if (!player || !player->IsLoaded())
        return;

    // Drag previews use a bounded, inexpensive seek, but the final mouse-up
    // position must be exact. With a long GOP, treating a nearby keyframe as
    // final can leave playback at its pre-seek position (commonly zero).
    // SeekWhilePlaying performs this work on the decode thread, so the UI stays
    // responsive while the decoder advances to the requested frame.
    player->SeekWhilePlaying(seconds, true);

    // Release the pinned cursor after this seek generation presents a frame.
    // A time-only >= check would release backward seeks before they land.
    g_previewReleaseAfterPresentation.store(
        player->GetPresentedPlaybackFrameCount() + 1,
        std::memory_order_relaxed);
    g_pendingPreviewReleasePlayer.store(player, std::memory_order_release);
}

void UpdateControls()
{
    if (!g_videoPlayer)
        return;

    bool isLoaded = g_videoPlayer->IsLoaded();
    bool isPlaying = g_videoPlayer->IsPlaying();

    EnableWindow(g_hButtonOpen, !g_isExporting);
    EnableWindow(g_hButtonPlay, isLoaded && !isPlaying);
    EnableWindow(g_hButtonPause, isLoaded && isPlaying);
    EnableWindow(g_hButtonStop, isLoaded);
    EnableWindow(g_hButtonSpeedDown, isLoaded);
    EnableWindow(g_hButtonSpeedUp, isLoaded);
    EnableWindow(g_hEditPlaybackSpeed, isLoaded);
    if (GetFocus() != g_hEditPlaybackSpeed)
    {
        wchar_t speedText[32];
        const double speed = g_videoPlayer->GetPlaybackSpeed();
        if (std::fabs(speed - std::round(speed)) < 0.001)
            swprintf_s(speedText, L"%.0fx", speed);
        else
            swprintf_s(speedText, L"%.1fx", speed);
        SetWindowTextW(g_hEditPlaybackSpeed, speedText);
    }
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
    bool hasValidDraft = hasStart && hasEnd && g_cutEndTime > g_cutStartTime;
    bool hasSavedClips = g_enableMultiClipEditing && !g_cutSegments.empty();
    bool hasSelectedClip = g_enableMultiClipEditing && g_selectedCutSegment >= 0 &&
                           g_selectedCutSegment < static_cast<int>(g_cutSegments.size());
    if (!hasStart && !hasEnd && !hasSavedClips)
    {
        SetWindowTextW(g_hButtonCut, L"Export Video");
        EnableWindow(g_hButtonCut, isLoaded && !g_isExporting);
    }
    else
    {
        SetWindowTextW(g_hButtonCut, L"Cut Video");
        EnableWindow(g_hButtonCut, isLoaded && !g_isExporting && (hasSavedClips || hasValidDraft));
    }
    EnableWindow(g_hButtonPlayClip, isLoaded && hasValidDraft);
    EnableWindow(g_hButtonPlayEnd, isLoaded && hasValidDraft);
    EnableWindow(g_hButtonAddClip, g_enableMultiClipEditing && isLoaded && hasValidDraft);
    EnableWindow(g_hButtonClearClips, g_enableMultiClipEditing && isLoaded && (hasSavedClips || hasStart || hasEnd));
    EnableWindow(g_hListBoxCutSegments, g_enableMultiClipEditing && isLoaded && hasSavedClips);
    EnableWindow(g_hButtonUpdateClip, g_enableMultiClipEditing && isLoaded && hasSelectedClip && hasValidDraft);
    EnableWindow(g_hButtonRemoveClip, g_enableMultiClipEditing && isLoaded && hasSelectedClip);
    EnableWindow(g_hButtonPlayAllClips, g_enableMultiClipEditing && isLoaded && hasSavedClips);

   bool canMerge = g_videoPlayer && g_videoPlayer->GetAudioTrackCount() > 1;
   EnableWindow(g_hCheckboxMergeAudio, isLoaded && canMerge);

   bool anyCrop = g_videoPlayer && g_videoPlayer->HasAnyCrop();
   bool hasCrop = g_videoPlayer && g_videoPlayer->hasCrop;
   size_t effectiveClipCount = g_enableMultiClipEditing
                               ? g_cutSegments.size() + (hasValidDraft && !hasSelectedClip ? 1u : 0u)
                               : (hasValidDraft ? 1u : 0u);
   bool requiresEncoding = anyCrop || effectiveClipCount > 1;
   EnableWindow(g_hRadioCopyCodec, isLoaded && !requiresEncoding);
   EnableWindow(g_hRadioH264, isLoaded);
   if (requiresEncoding) {
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
        int waveformProgress = GetAudioWaveformProgress();
        if (waveformProgress >= 0 && waveformProgress <= 100)
        {
            swprintf_s(statusText, _countof(statusText),
                       L"Time: %s / %s | Frame: %lld / %lld | %s | Waveforms: %d%%",
                       currentTimeStr.c_str(), durationStr.c_str(),
                       currentFrame, g_videoPlayer->GetTotalFrames(),
                       isPlaying ? L"Playing" : L"Paused",
                       waveformProgress);
        }
        else
        {
            swprintf_s(statusText, _countof(statusText),
                       L"Time: %s / %s | Frame: %lld / %lld | %s",
                       currentTimeStr.c_str(), durationStr.c_str(),
                       currentFrame, g_videoPlayer->GetTotalFrames(),
                       isPlaying ? L"Playing" : L"Paused");
        }
        SetWindowTextW(g_hStatusText, statusText);
    }
}

void UpdateTimeline()
{
    if (!g_videoPlayer || !g_videoPlayer->IsLoaded())
        return;

    // Release the preview pin once the actual playback position has caught up.
    // This keeps the cursor glued to the seek target during async refinement.
    if (g_previewSeekTime >= 0.0)
    {
        double frameDur = g_videoPlayer->frameRate > 0.0
                          ? (1.0 / g_videoPlayer->frameRate) : 0.033;
        const bool landedAtPreview =
            std::fabs(g_videoPlayer->GetCurrentTime() - g_previewSeekTime) <=
            frameDur * 1.5;
        const bool presentedAfterPlayingSeek =
            g_pendingPreviewReleasePlayer.load(std::memory_order_acquire) ==
                g_videoPlayer &&
            g_videoPlayer->GetPresentedPlaybackFrameCount() >=
                g_previewReleaseAfterPresentation.load(std::memory_order_relaxed);
        if (landedAtPreview || presentedAfterPlayingSeek)
        {
            g_previewSeekTime = -1.0;
            g_pendingPreviewReleasePlayer.store(nullptr, std::memory_order_release);
            g_previewReleaseAfterPresentation.store(0, std::memory_order_relaxed);

            // If playback was deferred until the seek completed, resume now.
            if (g_resumePlayAfterSeek)
            {
                g_resumePlayAfterSeek = false;
                g_videoPlayer->Play();
                UpdateControls();
            }
        }
    }

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
        // 1..1001 => -50.0dB..+50.0dB in 0.1dB steps, with 501 = 0.0dB
        int sliderPos = 501;
        if (volume <= 0.0f) {
            sliderPos = 0;
        } else {
            float db = 20.0f * log10f(volume);
            if (db < -50.0f) db = -50.0f;
            if (db > 50.0f) db = 50.0f;
            sliderPos = static_cast<int>(501.0f + (db * 10.0f));
            if (sliderPos < 1) sliderPos = 1;
            if (sliderPos > 1001) sliderPos = 1001;
        }

        SendMessage(g_hSliderTrackVolume, TBM_SETPOS, TRUE, sliderPos);
        
        // Update label with dB value
        wchar_t buf[64];
        if (sliderPos == 0) {
            swprintf_s(buf, L"Track Volume: Mute");
        } else {
            float dbDisplay = (sliderPos - 501.0f) / 10.0f;
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
        InvalidateRect(g_hTimeline, nullptr, FALSE);
    }
}

void OnTrackVolumeChanged(bool snapToZero)
{
    if (!g_videoPlayer)
        return;
    
    int selectedIndex = (int)SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
    if (selectedIndex != LB_ERR)
    {
        int sliderPos = (int)SendMessage(g_hSliderTrackVolume, TBM_GETPOS, 0, 0);
        
        // Magnetic snap to center for clicking and dragging. Wheel input bypasses
        // this so one 0.1 dB step can move away from 0.0 dB immediately.
        if (snapToZero && sliderPos >= 498 && sliderPos <= 504) {
            sliderPos = 501;
            SendMessage(g_hSliderTrackVolume, TBM_SETPOS, TRUE, sliderPos);
        }

        // Convert slider position to dB then to volume (amplitude)
        // 0 => hard mute
        // 1..1001 => -50dB..+50dB
        float db = 0.0f;
        float volume = 0.0f;
        if (sliderPos > 0)
        {
            db = (sliderPos - 501.0f) / 10.0f;
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
        InvalidateRect(g_hTimeline, nullptr, FALSE);
    }
}

void OnMasterVolumeChanged(bool snapToZero)
{
    if (!g_videoPlayer)
        return;
    
    int sliderPos = (int)SendMessage(g_hSliderMasterVolume, TBM_GETPOS, 0, 0);
    
    // Magnetic snap to center for clicking and dragging. Wheel input bypasses
    // this so one 0.1 dB step can move away from 0.0 dB immediately.
    if (snapToZero && sliderPos >= 498 && sliderPos <= 504) {
        sliderPos = 501;
        SendMessage(g_hSliderMasterVolume, TBM_SETPOS, TRUE, sliderPos);
    }

    // Convert slider position to dB then to volume (amplitude)
    // 0 => hard mute
    // 1..1001 => -50dB..+50dB
    float db = 0.0f;
    float volume = 0.0f;
    if (sliderPos > 0)
    {
        db = (sliderPos - 501.0f) / 10.0f;
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

void RefreshCutSegmentList()
{
    if (!g_hListBoxCutSegments)
        return;

    SendMessage(g_hListBoxCutSegments, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_cutSegments.size(); ++i) {
        std::wstring start = FormatTime(g_cutSegments[i].start, true);
        std::wstring end = FormatTime(g_cutSegments[i].end, true);
        wchar_t item[160];
        swprintf_s(item, _countof(item), L"%zu. %s - %s", i + 1, start.c_str(), end.c_str());
        SendMessage(g_hListBoxCutSegments, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }

    if (g_selectedCutSegment >= 0 && g_selectedCutSegment < static_cast<int>(g_cutSegments.size()))
        SendMessage(g_hListBoxCutSegments, LB_SETCURSEL, g_selectedCutSegment, 0);
}

void UpdateCutInfoLabel(HWND hwnd)
{
    wchar_t buffer[256];
    if (!g_enableMultiClipEditing)
    {
        std::wstring start = g_cutStartTime >= 0 ? FormatTime(g_cutStartTime, true) : L"Not set";
        std::wstring end = g_cutEndTime >= 0 ? FormatTime(g_cutEndTime, true) : L"Not set";
        swprintf_s(buffer, _countof(buffer), L"Start: %s\nEnd: %s", start.c_str(), end.c_str());
        SetWindowTextW(g_hLabelCutInfo, buffer);
        UpdateControls();
        UpdateCutTimeEdits();
        return;
    }

    double selectedDuration = 0.0;
    for (const auto& segment : g_cutSegments)
        selectedDuration += segment.end - segment.start;

    if (g_cutSegments.empty())
    {
        swprintf_s(buffer, L"No clips added.");
    }
    else
    {
        std::wstring durationStr = FormatTime(selectedDuration, true);
        swprintf_s(buffer, _countof(buffer), L"%zu clip%s - total %s",
                   g_cutSegments.size(), g_cutSegments.size() == 1 ? L"" : L"s",
                   durationStr.c_str());
    }
    SetWindowTextW(g_hLabelCutInfo, buffer);
    // Also update the cut button state
    UpdateControls();
    UpdateCutTimeEdits();
}
