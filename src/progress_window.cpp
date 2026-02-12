// progress_window.cpp - Minimal stubs for ImGui migration
#include "progress_window.h"

std::atomic<bool> g_cancelExport(false);
HWND g_hProgressBar = nullptr;
HWND g_hProgressWindow = nullptr;
HWND g_hProgressText = nullptr;
HWND g_hProgressPercentage = nullptr;

// All progress UI is now handled by imgui_ui.cpp
void ShowProgressWindow(HWND) {}
void CloseProgressWindow() {}
void UpdateProgressStatus(const std::wstring&) {}
LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return DefWindowProc(hwnd, msg, wParam, lParam); }
