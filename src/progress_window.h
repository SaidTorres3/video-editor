#ifndef PROGRESS_WINDOW_H
#define PROGRESS_WINDOW_H

#pragma once

#include <windows.h>
#include <atomic>

extern std::atomic<bool> g_cancelExport;
extern HWND g_hProgressBar;

void ShowProgressWindow(HWND parent);
void CloseProgressWindow();
LRESULT CALLBACK ProgressProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif // PROGRESS_WINDOW_H
