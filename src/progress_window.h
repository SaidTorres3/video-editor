#ifndef PROGRESS_WINDOW_H
#define PROGRESS_WINDOW_H

#include <windows.h>
#include <atomic>

extern HWND g_hProgressWnd;
extern HWND g_hProgressBar;
extern std::atomic<bool> g_cancelExport;

void ShowProgressWindow(HWND parent);
void UpdateProgress(int percent);
void CloseProgressWindow();
LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

#endif // PROGRESS_WINDOW_H
