#include "options_window.h"

// Forward declaration from main.cpp for styling
void ApplyDarkTheme(HWND hwnd);

static HWND g_hOptionsWnd = nullptr;

// Global option variables
bool g_useNvenc = false;
bool g_logToFile = true;
std::wstring g_b2KeyId;
std::wstring g_b2AppKey;
std::wstring g_b2BucketId;
std::wstring g_b2BucketName;
bool g_autoUpload = false;

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

        wchar_t buf[256];
        DWORD sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"B2KeyId", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_b2KeyId = buf;
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"B2AppKey", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_b2AppKey = buf;
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"B2BucketId", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_b2BucketId = buf;
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"B2BucketName", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_b2BucketName = buf;
        sz = sizeof(DWORD); val = 0;
        if (RegQueryValueExW(hKey, L"AutoUpload", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            g_autoUpload = val != 0;
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
        RegSetValueExW(hKey, L"B2KeyId", 0, REG_SZ, (const BYTE*)g_b2KeyId.c_str(), (DWORD)((g_b2KeyId.size()+1)*sizeof(wchar_t)));
        RegSetValueExW(hKey, L"B2AppKey", 0, REG_SZ, (const BYTE*)g_b2AppKey.c_str(), (DWORD)((g_b2AppKey.size()+1)*sizeof(wchar_t)));
        RegSetValueExW(hKey, L"B2BucketId", 0, REG_SZ, (const BYTE*)g_b2BucketId.c_str(), (DWORD)((g_b2BucketId.size()+1)*sizeof(wchar_t)));
        RegSetValueExW(hKey, L"B2BucketName", 0, REG_SZ, (const BYTE*)g_b2BucketName.c_str(), (DWORD)((g_b2BucketName.size()+1)*sizeof(wchar_t)));
        val = g_autoUpload ? 1 : 0;
        RegSetValueExW(hKey, L"AutoUpload", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
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
                                   CW_USEDEFAULT, CW_USEDEFAULT, 260, 200,
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
    HWND hB2 = CreateWindow(L"BUTTON", L"B2 Settings",
                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            10, 120, 100, 25, g_hOptionsWnd,
                            (HMENU)ID_BUTTON_B2_CONFIG,
                            (HINSTANCE)GetWindowLongPtr(g_hOptionsWnd, GWLP_HINSTANCE), nullptr);
    HWND hOk = CreateWindow(L"BUTTON", L"OK",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            120, 120, 60, 25, g_hOptionsWnd,
                            (HMENU)IDOK,
                            (HINSTANCE)GetWindowLongPtr(g_hOptionsWnd, GWLP_HINSTANCE), nullptr);
    HWND hCancel = CreateWindow(L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                190, 120, 60, 25, g_hOptionsWnd,
                                (HMENU)IDCANCEL,
                                (HINSTANCE)GetWindowLongPtr(g_hOptionsWnd, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(hOk);
    ApplyDarkTheme(hCancel);
    ApplyDarkTheme(hB2);

    SendMessage(hLib, BM_SETCHECK, g_useNvenc ? BST_UNCHECKED : BST_CHECKED, 0);
    SendMessage(hNv, BM_SETCHECK, g_useNvenc ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(hLog, BM_SETCHECK, g_logToFile ? BST_CHECKED : BST_UNCHECKED, 0);
}

LRESULT CALLBACK OptionsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BUTTON_B2_CONFIG) {
            ShowB2ConfigWindow(hwnd);
        } else if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
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

// ---------------- B2 Configuration Window -----------------

static HWND g_hB2Wnd = nullptr;

void ShowB2ConfigWindow(HWND parent)
{
    if (g_hB2Wnd) { SetForegroundWindow(g_hB2Wnd); return; }

    g_hB2Wnd = CreateWindowEx(0, L"B2ConfigClass", L"Backblaze B2 Settings",
                              WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 340, 230,
                              parent, nullptr,
                              (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hB2Wnd);

    CreateWindow(L"STATIC", L"Key ID:", WS_CHILD | WS_VISIBLE,
                 10, 10, 100, 20, g_hB2Wnd, nullptr,
                 (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);
    HWND hKeyId = CreateWindow(L"EDIT", g_b2KeyId.c_str(),
                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                               120, 10, 190, 20, g_hB2Wnd,
                               (HMENU)ID_EDIT_B2_KEY_ID,
                               (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);

    CreateWindow(L"STATIC", L"App Key:", WS_CHILD | WS_VISIBLE,
                 10, 40, 100, 20, g_hB2Wnd, nullptr,
                 (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);
    HWND hAppKey = CreateWindow(L"EDIT", g_b2AppKey.c_str(),
                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                               120, 40, 190, 20, g_hB2Wnd,
                               (HMENU)ID_EDIT_B2_APP_KEY,
                               (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);

    CreateWindow(L"STATIC", L"Bucket ID:", WS_CHILD | WS_VISIBLE,
                 10, 70, 100, 20, g_hB2Wnd, nullptr,
                 (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);
    HWND hBucketId = CreateWindow(L"EDIT", g_b2BucketId.c_str(),
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                 120, 70, 190, 20, g_hB2Wnd,
                                 (HMENU)ID_EDIT_B2_BUCKET_ID,
                                 (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);

    CreateWindow(L"STATIC", L"Bucket Name:", WS_CHILD | WS_VISIBLE,
                 10, 100, 100, 20, g_hB2Wnd, nullptr,
                 (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);
    HWND hBucketName = CreateWindow(L"EDIT", g_b2BucketName.c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                   120, 100, 190, 20, g_hB2Wnd,
                                   (HMENU)ID_EDIT_B2_BUCKET_NAME,
                                   (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);

    HWND hAuto = CreateWindow(L"BUTTON", L"Auto upload after export",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              10, 130, 200, 20, g_hB2Wnd,
                              (HMENU)ID_CHECKBOX_AUTO_UPLOAD,
                              (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);

    HWND hOk = CreateWindow(L"BUTTON", L"OK",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            70, 170, 80, 25, g_hB2Wnd,
                            (HMENU)IDOK,
                            (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);
    HWND hCancel = CreateWindow(L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                170, 170, 80, 25, g_hB2Wnd,
                                (HMENU)IDCANCEL,
                                (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);

    ApplyDarkTheme(hKeyId);
    ApplyDarkTheme(hAppKey);
    ApplyDarkTheme(hBucketId);
    ApplyDarkTheme(hBucketName);
    ApplyDarkTheme(hAuto);
    ApplyDarkTheme(hOk);
    ApplyDarkTheme(hCancel);

    SendMessage(hAuto, BM_SETCHECK, g_autoUpload ? BST_CHECKED : BST_UNCHECKED, 0);
}

LRESULT CALLBACK B2ConfigProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            wchar_t buf[256];
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_B2_KEY_ID), buf, 256);
            g_b2KeyId = buf;
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_B2_APP_KEY), buf, 256);
            g_b2AppKey = buf;
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_B2_BUCKET_ID), buf, 256);
            g_b2BucketId = buf;
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_B2_BUCKET_NAME), buf, 256);
            g_b2BucketName = buf;
            g_autoUpload = SendMessage(GetDlgItem(hwnd, ID_CHECKBOX_AUTO_UPLOAD), BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveSettings();
            DestroyWindow(hwnd);
        }
        break;
    case WM_CLOSE:
        {
            wchar_t buf[256];
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_B2_KEY_ID), buf, 256);
            g_b2KeyId = buf;
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_B2_APP_KEY), buf, 256);
            g_b2AppKey = buf;
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_B2_BUCKET_ID), buf, 256);
            g_b2BucketId = buf;
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_B2_BUCKET_NAME), buf, 256);
            g_b2BucketName = buf;
            g_autoUpload = SendMessage(GetDlgItem(hwnd, ID_CHECKBOX_AUTO_UPLOAD), BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveSettings();
            DestroyWindow(hwnd);
        }
        break;
    case WM_DESTROY:
        g_hB2Wnd = nullptr;
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
