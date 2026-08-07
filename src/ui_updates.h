#pragma once

#include <windows.h>

class VideoPlayer;

void UpdateControls();
void UpdateTimeline();
void FinalizePlayingUiSeek(VideoPlayer* player, double seconds);
void UpdateAudioTrackList();
void OnAudioTrackSelectionChanged();
void OnMuteTrackClicked();
void OnTrackVolumeChanged(bool snapToZero = true);
void OnMasterVolumeChanged(bool snapToZero = true);
void UpdateCutInfoLabel(HWND hwnd);
void UpdateCutTimeEdits();
void RefreshCutSegmentList();
