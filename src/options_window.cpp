#include "options_window.h"

// Forward declaration from main.cpp for styling
void ApplyDarkTheme(HWND hwnd);

static HWND g_hOptionsWnd = nullptr;

// Global option variables
bool g_useNvenc = false;
bool g_logToFile = true;

// Load settings from Windows registry
void LoadSettings()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\VideoEditor", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val; DWORD size = sizeof(val);
        if (RegQueryValueExW(hKey, L"UseNvenc", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_useNvenc = (val != 0);
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"EnableLogFile", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_logToFile = (val != 0);
        RegCloseKey(hKey);
    }
}

// Save settings to Windows registry
void SaveSettings()
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\VideoEditor", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD val = g_useNvenc ? 1 : 0;
        RegSetValueExW(hKey, L"UseNvenc", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_logToFile ? 1 : 0;
        RegSetValueExW(hKey, L"EnableLogFile", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        RegCloseKey(hKey);
    }
}

void ShowOptionsWindow(HWND parent)
{
    if (g_hOptionsWnd) {
        SetForegroundWindow(g_hOptionsWnd);
        return;
    }

    g_hOptionsWnd = CreateWindowEx(0, L"OptionsClass", L"Options",
                                   WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 220, 180,
                                   parent, nullptr,
                                   (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hOptionsWnd);

    CreateWindow(L"STATIC", L"Encode H264:", WS_CHILD | WS_VISIBLE,
                 10, 10, 120, 20, g_hOptionsWnd, nullptr,
                 (HINSTANCE)GetWindowLongPtr(g_hOptionsWnd, GWLP_HINSTANCE), nullptr);
    HWND hLib = CreateWindow(L"BUTTON", L"libx264",
                             WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                             10, 40, 100, 20, g_hOptionsWnd,
                             (HMENU)ID_RADIO_ENCODER_LIBX264,
                             (HINSTANCE)GetWindowLongPtr(g_hOptionsWnd, GWLP_HINSTANCE), nullptr);
    HWND hNv = CreateWindow(L"BUTTON", L"NVENC h264",
                            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                            10, 65, 120, 20, g_hOptionsWnd,
                            (HMENU)ID_RADIO_ENCODER_NVENC,
                            (HINSTANCE)GetWindowLongPtr(g_hOptionsWnd, GWLP_HINSTANCE), nullptr);
    HWND hLog = CreateWindow(L"BUTTON", L"Enable log file",
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             10, 90, 150, 20, g_hOptionsWnd,
                             (HMENU)ID_CHECKBOX_ENABLE_LOG,
                             (HINSTANCE)GetWindowLongPtr(g_hOptionsWnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(hLib);
    ApplyDarkTheme(hNv);
    ApplyDarkTheme(hLog);
    HWND hOk = CreateWindow(L"BUTTON", L"OK",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            30, 120, 70, 25, g_hOptionsWnd,
                            (HMENU)IDOK,
                            (HINSTANCE)GetWindowLongPtr(g_hOptionsWnd, GWLP_HINSTANCE), nullptr);
    HWND hCancel = CreateWindow(L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                110, 120, 70, 25, g_hOptionsWnd,
                                (HMENU)IDCANCEL,
                                (HINSTANCE)GetWindowLongPtr(g_hOptionsWnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(hOk);
    ApplyDarkTheme(hCancel);

    SendMessage(hLib, BM_SETCHECK, g_useNvenc ? BST_UNCHECKED : BST_CHECKED, 0);
    SendMessage(hNv, BM_SETCHECK, g_useNvenc ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(hLog, BM_SETCHECK, g_logToFile ? BST_CHECKED : BST_UNCHECKED, 0);
}

LRESULT CALLBACK OptionsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            HWND hNv = GetDlgItem(hwnd, ID_RADIO_ENCODER_NVENC);
            HWND hLog = GetDlgItem(hwnd, ID_CHECKBOX_ENABLE_LOG);
            g_useNvenc = SendMessage(hNv, BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_logToFile = SendMessage(hLog, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveSettings();
            DestroyWindow(hwnd);
        }
        break;
    case WM_CLOSE:
        {
            HWND hNv = GetDlgItem(hwnd, ID_RADIO_ENCODER_NVENC);
            HWND hLog = GetDlgItem(hwnd, ID_CHECKBOX_ENABLE_LOG);
            g_useNvenc = SendMessage(hNv, BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_logToFile = SendMessage(hLog, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveSettings();
            DestroyWindow(hwnd);
        }
        break;
    case WM_DESTROY:
        g_hOptionsWnd = nullptr;
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
