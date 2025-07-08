#pragma once

#include <windows.h>

void UpdateControls();
void UpdateTimeline();
void UpdateAudioTrackList();
void OnAudioTrackSelectionChanged();
void OnMuteTrackClicked();
void OnTrackVolumeChanged();
void OnMasterVolumeChanged();
void UpdateCutInfoLabel(HWND hwnd);
void UpdateCutTimeEdits();
