#ifndef PROGRESS_WINDOW_H
#define PROGRESS_WINDOW_H

#pragma once

#include <windows.h>
#include <atomic>
#include <string>

extern std::atomic<bool> g_cancelExport;
extern HWND g_hProgressBar;
extern HWND g_hProgressWindow;
extern HWND g_hProgressText;
extern HWND g_hProgressPercentage;

void ShowProgressWindow(HWND parent);
void CloseProgressWindow();
void UpdateProgressStatus(const std::wstring& status);
LRESULT CALLBACK ProgressProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif // PROGRESS_WINDOW_H
