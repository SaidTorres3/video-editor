#include "progress_window.h"
#include <commctrl.h>
#include "utils.h"

std::atomic<bool> g_cancelExport(false);
HWND g_hProgressBar = nullptr;
HWND g_hProgressWindow = nullptr;

void ShowProgressWindow(HWND parent) {
    if (g_hProgressWindow) {
        SetWindowTextW(g_hProgressWindow, L"Exporting video");
        ShowWindow(g_hProgressWindow, SW_SHOW);
        UpdateWindow(g_hProgressWindow);
        return;
    }

    g_hProgressWindow = CreateWindowEx(
        WS_EX_TOPMOST, L"ProgressClass", L"Exporting video",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 150,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);

    if (g_hProgressWindow) {
        CenterWindow(g_hProgressWindow, parent);
        ShowWindow(g_hProgressWindow, SW_SHOW);
        UpdateWindow(g_hProgressWindow);
    }
}

void CloseProgressWindow() {
    if (g_hProgressWindow) {
        DestroyWindow(g_hProgressWindow);
        g_hProgressWindow = nullptr;
    }
}

LRESULT CALLBACK ProgressProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            g_hProgressBar = CreateWindowEx(
                0, PROGRESS_CLASS, nullptr,
                WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                20, 20, 250, 25,
                hwnd, (HMENU)1, GetModuleHandle(nullptr), nullptr);
            SendMessage(g_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

            HWND hCancelButton = CreateWindow(
                L"BUTTON", L"Cancel",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                100, 60, 80, 30,
                hwnd, (HMENU)2, // IDC_CANCEL_EXPORT
                (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

            // Apply dark theme if available
            // ApplyDarkTheme(hwnd);
            // ApplyDarkTheme(hCancelButton);
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == 2) { // Cancel button clicked
                g_cancelExport = true;
            }
            break;

        case WM_CLOSE:
            g_cancelExport = true;
            break;

        case WM_DESTROY:
            g_hProgressBar = nullptr;
            g_hProgressWindow = nullptr;
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}