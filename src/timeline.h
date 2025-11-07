#pragma once

#include <windows.h>

LRESULT CALLBACK TimelineProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Timeline zoom variables
extern double g_timelineZoomLevel;
extern double g_timelineScrollOffset;
