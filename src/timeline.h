#pragma once

#include <windows.h>

LRESULT CALLBACK TimelineProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Timeline zoom variables
extern double g_timelineZoomLevel;
extern double g_timelineScrollOffset;

// Called after a new video is loaded so the thumbnail thread pre-warms the cache.
void TriggerThumbnailPreCache(double duration);

// Rebuilds or clears the cached audio waveform according to the current setting.
// Decoding happens on a background thread and never blocks the UI.
void RefreshAudioWaveformPreview();
