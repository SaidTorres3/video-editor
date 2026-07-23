#pragma once

#include <windows.h>

LRESULT CALLBACK TimelineProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK TimelineResizeBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// User-adjustable timeline height. The layout may temporarily clamp this when
// the main window is too small, while preserving the preferred height.
int GetPreferredTimelineHeight();

// Timeline zoom variables
extern double g_timelineZoomLevel;
extern double g_timelineScrollOffset;

// Called after a new video is loaded so the thumbnail thread pre-warms the cache.
void TriggerThumbnailPreCache(double duration);

// Rebuilds or clears the cached audio waveform according to the current setting.
// Decoding happens on a background thread and never blocks the UI.
void RefreshAudioWaveformPreview();
