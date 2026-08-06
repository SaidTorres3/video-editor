#pragma once

#include <windows.h>

class VideoPlayer;

void UpdateControls();
void UpdateTimeline();
void FinalizePlayingUiSeek(VideoPlayer* player, double seconds);
void UpdateAudioTrackList();
void OnAudioTrackSelectionChanged();
void OnMuteTrackClicked();
void OnTrackVolumeChanged();
void OnMasterVolumeChanged();
void UpdateCutInfoLabel(HWND hwnd);
void UpdateCutTimeEdits();
void RefreshCutSegmentList();
