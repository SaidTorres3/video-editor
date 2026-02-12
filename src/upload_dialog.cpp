// upload_dialog.cpp - Minimal stubs for ImGui migration
#include "upload_dialog.h"

// All upload UI is now handled by imgui_ui.cpp
void ShowUrlCopyDialog(HWND, const std::wstring&, const std::wstring&, const std::wstring&) {}
LRESULT CALLBACK UrlCopyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return DefWindowProc(hwnd, msg, wParam, lParam); }
void ShowManualUploadDialog(HWND, const std::wstring&, bool, bool) {}
LRESULT CALLBACK ManualUploadProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return DefWindowProc(hwnd, msg, wParam, lParam); }
