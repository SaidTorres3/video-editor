#include "progress_window.h"
#include <commctrl.h>
#include "utils.h"

std::atomic<bool> g_cancelExport(false);
HWND g_hProgressBar = nullptr;
HWND g_hProgressWindow = nullptr;
HWND g_hProgressText = nullptr;
HWND g_hProgressPercentage = nullptr;

// Dark mode colors
const COLORREF DARK_BG = RGB(45, 45, 48);
const COLORREF DARK_TEXT = RGB(220, 220, 220);

const int WINDOW_WIDTH = 420;
const int WINDOW_HEIGHT = 180;

void ShowProgressWindow(HWND parent) {
    if (g_hProgressWindow) {
        SetWindowTextW(g_hProgressWindow, L"Processing Video");
        ShowWindow(g_hProgressWindow, SW_SHOW);
        UpdateWindow(g_hProgressWindow);
        g_cancelExport = false;
        return;
    }

    g_hProgressWindow = CreateWindowEx(
        WS_EX_TOPMOST, L"ProgressClass", L"Processing Video",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);

    if (g_hProgressWindow) {
        CenterWindow(g_hProgressWindow, parent);
        g_cancelExport = false;
        ShowWindow(g_hProgressWindow, SW_SHOW);
        UpdateWindow(g_hProgressWindow);
    }
}

void CloseProgressWindow() {
    if (g_hProgressWindow) {
        DestroyWindow(g_hProgressWindow);
        g_hProgressWindow = nullptr;
        g_hProgressBar = nullptr;
        g_hProgressText = nullptr;
        g_hProgressPercentage = nullptr;
    }
}

void UpdateProgressStatus(const std::wstring& status) {
    if (g_hProgressText && IsWindow(g_hProgressText)) {
        SetWindowTextW(g_hProgressText, status.c_str());
    }
}

LRESULT CALLBACK ProgressProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // Main status text
            g_hProgressText = CreateWindowEx(
                0, L"STATIC", L"Initializing...",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                15, 15, 370, 20,
                hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
            
            // Percentage text
            g_hProgressPercentage = CreateWindowEx(
                0, L"STATIC", L"0%",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                15, 40, 370, 20,
                hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

            // Progress bar
            g_hProgressBar = CreateWindowEx(
                0, PROGRESS_CLASS, nullptr,
                WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                15, 65, 370, 25,
                hwnd, (HMENU)1, GetModuleHandle(nullptr), nullptr);
            SendMessage(g_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessage(g_hProgressBar, PBM_SETSTEP, 1, 0);

            // Cancel button
            HWND hCancelButton = CreateWindow(
                L"BUTTON", L"Cancel",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                WINDOW_WIDTH / 2 - 40, 100, 80, 30,
                hwnd, (HMENU)2,
                (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);

            // Apply font to all controls
            HFONT hFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            
            if (hFont) {
                SendMessage(g_hProgressText, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessage(g_hProgressPercentage, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessage(hCancelButton, WM_SETFONT, (WPARAM)hFont, TRUE);
            }
            break;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == 2) { // Cancel button clicked
                g_cancelExport = true;
                EnableWindow((HWND)lParam, FALSE);
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Draw dark background
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            HBRUSH hBgBrush = CreateSolidBrush(DARK_BG);
            FillRect(hdc, &clientRect, hBgBrush);
            DeleteObject(hBgBrush);

            EndPaint(hwnd, &ps);
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            
            // Set dark background and light text for static controls
            SetBkColor(hdc, DARK_BG);
            SetTextColor(hdc, DARK_TEXT);
            
            return (LRESULT)CreateSolidBrush(DARK_BG);
        }

        case WM_CLOSE:
            g_cancelExport = true;
            break;

        case WM_DESTROY:
            g_hProgressBar = nullptr;
            g_hProgressWindow = nullptr;
            g_hProgressText = nullptr;
            g_hProgressPercentage = nullptr;
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}
