#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "clip_segment.h"

void OnSetStartClicked(HWND hwnd);
void OnSetEndClicked(HWND hwnd);
void OnPlayClipClicked(HWND hwnd);
void OnPlayEndClipClicked(HWND hwnd);
void OnAddClipClicked(HWND hwnd);
void OnUpdateClipClicked(HWND hwnd);
void OnRemoveClipClicked(HWND hwnd);
void OnClearClipsClicked(HWND hwnd);
void OnPlayAllClipsClicked(HWND hwnd);
void OnCutSegmentSelectionChanged(HWND hwnd);
void OnCutClicked(HWND hwnd);
void OnExportClicked(HWND hwnd);

extern std::vector<ClipSegment> g_cutSegments;
extern int g_selectedCutSegment;
extern bool g_isExporting;

extern bool g_lastOperationWasExport;
extern bool g_uploadSuccess;
extern std::wstring g_catboxUploadedUrl;
extern std::wstring g_b2UploadedUrl;
extern bool g_catboxUploadSuccess;
extern bool g_b2UploadSuccess;
extern std::wstring g_lastOutputFile;
