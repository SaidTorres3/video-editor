// editing.cpp - Global variable definitions for editing state
// All UI logic has been moved to imgui_ui.cpp
#include "editing.h"
#include "video_player.h"
#include "options_window.h"
#include <string>

// Global variables
std::wstring g_catboxUploadedUrl;
std::wstring g_b2UploadedUrl;
bool g_catboxUploadSuccess = false;
bool g_b2UploadSuccess = false;
bool g_uploadSuccess = false;
bool g_lastOperationWasExport = false;
std::wstring g_lastOutputFile;

// These functions are no longer used - cut/export is handled by imgui_ui.cpp
void OnSetStartClicked(HWND) {}
void OnSetEndClicked(HWND) {}
void OnPlayClipClicked(HWND) {}
void OnPlayEndClipClicked(HWND) {}
void OnCutClicked(HWND) {}
void OnExportClicked(HWND) {}
