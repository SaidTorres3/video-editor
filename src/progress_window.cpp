#include "progress_window.h"
#include <commctrl.h>
#include <uxtheme.h>
#pragma comment(lib, "comctl32.lib")

HWND g_hProgressWnd = nullptr;
HWND g_hProgressBar = nullptr;
std::atomic<bool> g_cancelExport{false};

// Forward declaration from main.cpp for styling
void ApplyDarkTheme(HWND hwnd);

void ShowProgressWindow(HWND parent)
{
    INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&ic);
    g_cancelExport = false;
    g_hProgressWnd = CreateWindowEx(WS_EX_TOPMOST, L"ProgressClass", L"Exporting", WS_CAPTION | WS_POPUPWINDOW,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 300, 100,
                                   parent, nullptr, (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hProgressWnd);
    g_hProgressBar = CreateWindowEx(0, PROGRESS_CLASS, nullptr,
                                   WS_CHILD | WS_VISIBLE, 20, 30, 260, 20,
                                   g_hProgressWnd, nullptr, (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    SendMessage(g_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    ShowWindow(g_hProgressWnd, SW_SHOW);
    UpdateWindow(g_hProgressWnd);
}

void UpdateProgress(int percent)
{
    if (g_hProgressBar)
        SendMessage(g_hProgressBar, PBM_SETPOS, percent, 0);
}

void CloseProgressWindow()
{
    if (g_hProgressWnd)
    {
        DestroyWindow(g_hProgressWnd);
        g_hProgressWnd = nullptr;
        g_hProgressBar = nullptr;
    }
}

LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CLOSE:
        g_cancelExport = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (hwnd == g_hProgressWnd)
        {
            g_hProgressWnd = nullptr;
            g_hProgressBar = nullptr;
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
