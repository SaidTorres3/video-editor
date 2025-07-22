#include "options_window.h"

// Forward declaration from main.cpp for styling
void ApplyDarkTheme(HWND hwnd);

static HWND g_hOptionsWnd = nullptr;
static HWND g_hUploadWnd = nullptr;
static HWND g_hCatboxWnd = nullptr;

// Global option variables
bool g_useNvenc = false;
bool g_logToFile = true;
std::wstring g_b2KeyId;
std::wstring g_b2AppKey;
std::wstring g_b2BucketId;
std::wstring g_b2BucketName;
std::wstring g_b2CustomUrl;
bool g_autoUpload = false;
bool g_useCatbox = false;
bool g_useB2 = true;
std::wstring g_catboxUserHash;

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
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"B2CustomUrl", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_b2CustomUrl = buf;
        sz = sizeof(DWORD); val = 0;
        if (RegQueryValueExW(hKey, L"AutoUpload", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            g_autoUpload = val != 0;
        sz = sizeof(DWORD); val = 0;
        if (RegQueryValueExW(hKey, L"UseCatbox", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            g_useCatbox = val != 0;
        sz = sizeof(DWORD); val = 1;
        if (RegQueryValueExW(hKey, L"UseB2", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            g_useB2 = val != 0;
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"CatboxHash", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_catboxUserHash = buf;
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
        RegSetValueExW(hKey, L"B2CustomUrl", 0, REG_SZ, (const BYTE*)g_b2CustomUrl.c_str(), (DWORD)((g_b2CustomUrl.size()+1)*sizeof(wchar_t)));
        val = g_autoUpload ? 1 : 0;
        RegSetValueExW(hKey, L"AutoUpload", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_useCatbox ? 1 : 0;
        RegSetValueExW(hKey, L"UseCatbox", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_useB2 ? 1 : 0;
        RegSetValueExW(hKey, L"UseB2", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        RegSetValueExW(hKey, L"CatboxHash", 0, REG_SZ, (const BYTE*)g_catboxUserHash.c_str(), (DWORD)((g_catboxUserHash.size()+1)*sizeof(wchar_t)));
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
                                   CW_USEDEFAULT, CW_USEDEFAULT, 280, 200,
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
    HWND hUpload = CreateWindow(L"BUTTON", L"Upload Settings",
                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                               10, 120, 100, 25, g_hOptionsWnd,
                               (HMENU)ID_BUTTON_UPLOAD_CONFIG,
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
    ApplyDarkTheme(hUpload);

    SendMessage(hLib, BM_SETCHECK, g_useNvenc ? BST_UNCHECKED : BST_CHECKED, 0);
    SendMessage(hNv, BM_SETCHECK, g_useNvenc ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(hLog, BM_SETCHECK, g_logToFile ? BST_CHECKED : BST_UNCHECKED, 0);
}

LRESULT CALLBACK OptionsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BUTTON_UPLOAD_CONFIG) {
            ShowUploadWindow(hwnd);
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

    g_hB2Wnd = CreateWindowEx(0, L"B2ConfigClass", L"Upload Settings",
                              WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 340, 300,
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

    CreateWindow(L"STATIC", L"Custom URL:", WS_CHILD | WS_VISIBLE,
                 10, 130, 100, 20, g_hB2Wnd, nullptr,
                 (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);
    HWND hCustomUrl = CreateWindow(L"EDIT", g_b2CustomUrl.c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                  120, 130, 190, 20, g_hB2Wnd,
                                  (HMENU)ID_EDIT_B2_CUSTOM_URL,
                                  (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);

    HWND hEnableB2 = CreateWindow(L"BUTTON", L"Enable Backblaze B2", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                  10, 160, 180, 20, g_hB2Wnd,
                                  (HMENU)ID_CHECKBOX_USE_B2,
                                  (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);

    HWND hOk = CreateWindow(L"BUTTON", L"OK",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            70, 200, 80, 25, g_hB2Wnd,
                            (HMENU)IDOK,
                            (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);
    HWND hCancel = CreateWindow(L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                170, 200, 80, 25, g_hB2Wnd,
                                (HMENU)IDCANCEL,
                                (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE), nullptr);

    ApplyDarkTheme(hKeyId);
    ApplyDarkTheme(hAppKey);
    ApplyDarkTheme(hBucketId);
    ApplyDarkTheme(hBucketName);
    ApplyDarkTheme(hCustomUrl);
    ApplyDarkTheme(hEnableB2);
    ApplyDarkTheme(hOk);
    ApplyDarkTheme(hCancel);
    SendMessage(hEnableB2, BM_SETCHECK, g_useB2 ? BST_CHECKED : BST_UNCHECKED, 0);
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
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_B2_CUSTOM_URL), buf, 256);
            g_b2CustomUrl = buf;
            g_useB2 = SendMessage(GetDlgItem(hwnd, ID_CHECKBOX_USE_B2), BM_GETCHECK, 0, 0) == BST_CHECKED;
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
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_B2_CUSTOM_URL), buf, 256);
            g_b2CustomUrl = buf;
            g_useB2 = SendMessage(GetDlgItem(hwnd, ID_CHECKBOX_USE_B2), BM_GETCHECK, 0, 0) == BST_CHECKED;
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

// ---------------- Upload Settings Window -----------------

void ShowUploadWindow(HWND parent)
{
    if (g_hUploadWnd) { SetForegroundWindow(g_hUploadWnd); return; }

    g_hUploadWnd = CreateWindowEx(0, L"UploadConfigClass", L"Upload Settings",
                                  WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 300, 150,
                                  parent, nullptr,
                                  (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hUploadWnd);

    HWND hAuto = CreateWindow(L"BUTTON", L"Auto upload after export",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              10, 10, 200, 20, g_hUploadWnd,
                              (HMENU)ID_CHECKBOX_AUTO_UPLOAD,
                              (HINSTANCE)GetWindowLongPtr(g_hUploadWnd, GWLP_HINSTANCE), nullptr);

    HWND hCatbox = CreateWindow(L"BUTTON", L"Catbox Settings",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                10, 40, 110, 25, g_hUploadWnd,
                                (HMENU)ID_BUTTON_CATBOX_CONFIG,
                                (HINSTANCE)GetWindowLongPtr(g_hUploadWnd, GWLP_HINSTANCE), nullptr);

    HWND hB2 = CreateWindow(L"BUTTON", L"Backblaze B2 Settings",
                             WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             130, 40, 130, 25, g_hUploadWnd,
                             (HMENU)ID_BUTTON_B2_SETTINGS,
                             (HINSTANCE)GetWindowLongPtr(g_hUploadWnd, GWLP_HINSTANCE), nullptr);

    HWND hOk = CreateWindow(L"BUTTON", L"OK",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            40, 80, 80, 25, g_hUploadWnd,
                            (HMENU)IDOK,
                            (HINSTANCE)GetWindowLongPtr(g_hUploadWnd, GWLP_HINSTANCE), nullptr);
    HWND hCancel = CreateWindow(L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                140, 80, 80, 25, g_hUploadWnd,
                                (HMENU)IDCANCEL,
                                (HINSTANCE)GetWindowLongPtr(g_hUploadWnd, GWLP_HINSTANCE), nullptr);

    ApplyDarkTheme(hAuto);
    ApplyDarkTheme(hCatbox);
    ApplyDarkTheme(hB2);
    ApplyDarkTheme(hOk);
    ApplyDarkTheme(hCancel);

    SendMessage(hAuto, BM_SETCHECK, g_autoUpload ? BST_CHECKED : BST_UNCHECKED, 0);
}

LRESULT CALLBACK UploadProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BUTTON_CATBOX_CONFIG:
            ShowCatboxConfigWindow(hwnd);
            break;
        case ID_BUTTON_B2_SETTINGS:
            ShowB2ConfigWindow(hwnd);
            break;
        case IDOK:
        case IDCANCEL:
            g_autoUpload = SendMessage(GetDlgItem(hwnd, ID_CHECKBOX_AUTO_UPLOAD), BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveSettings();
            DestroyWindow(hwnd);
            break;
        }
        break;
    case WM_CLOSE:
        g_autoUpload = SendMessage(GetDlgItem(hwnd, ID_CHECKBOX_AUTO_UPLOAD), BM_GETCHECK, 0, 0) == BST_CHECKED;
        SaveSettings();
        DestroyWindow(hwnd);
        break;
    case WM_DESTROY:
        g_hUploadWnd = nullptr;
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------- Catbox Settings Window -----------------

void ShowCatboxConfigWindow(HWND parent)
{
    if (g_hCatboxWnd) { SetForegroundWindow(g_hCatboxWnd); return; }

    g_hCatboxWnd = CreateWindowEx(0, L"CatboxConfigClass", L"Catbox Settings",
                                  WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 300, 150,
                                  parent, nullptr,
                                  (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hCatboxWnd);

    CreateWindow(L"STATIC", L"User Hash:", WS_CHILD | WS_VISIBLE,
                 10, 10, 100, 20, g_hCatboxWnd, nullptr,
                 (HINSTANCE)GetWindowLongPtr(g_hCatboxWnd, GWLP_HINSTANCE), nullptr);
    HWND hHash = CreateWindow(L"EDIT", g_catboxUserHash.c_str(),
                              WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                              120, 10, 160, 20, g_hCatboxWnd,
                              (HMENU)ID_EDIT_CATBOX_HASH,
                              (HINSTANCE)GetWindowLongPtr(g_hCatboxWnd, GWLP_HINSTANCE), nullptr);

    HWND hEnable = CreateWindow(L"BUTTON", L"Enable catbox.moe", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                10, 40, 180, 20, g_hCatboxWnd,
                                (HMENU)ID_CHECKBOX_USE_CATBOX,
                                (HINSTANCE)GetWindowLongPtr(g_hCatboxWnd, GWLP_HINSTANCE), nullptr);

    HWND hOk = CreateWindow(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            60, 80, 80, 25, g_hCatboxWnd, (HMENU)IDOK,
                            (HINSTANCE)GetWindowLongPtr(g_hCatboxWnd, GWLP_HINSTANCE), nullptr);
    HWND hCancel = CreateWindow(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                160, 80, 80, 25, g_hCatboxWnd, (HMENU)IDCANCEL,
                                (HINSTANCE)GetWindowLongPtr(g_hCatboxWnd, GWLP_HINSTANCE), nullptr);

    ApplyDarkTheme(hHash);
    ApplyDarkTheme(hEnable);
    ApplyDarkTheme(hOk);
    ApplyDarkTheme(hCancel);

    SendMessage(hEnable, BM_SETCHECK, g_useCatbox ? BST_CHECKED : BST_UNCHECKED, 0);
}

LRESULT CALLBACK CatboxConfigProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            wchar_t buf[256];
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_CATBOX_HASH), buf, 256);
            g_catboxUserHash = buf;
            g_useCatbox = SendMessage(GetDlgItem(hwnd, ID_CHECKBOX_USE_CATBOX), BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveSettings();
            DestroyWindow(hwnd);
        }
        break;
    case WM_CLOSE:
        {
            wchar_t buf[256];
            GetWindowTextW(GetDlgItem(hwnd, ID_EDIT_CATBOX_HASH), buf, 256);
            g_catboxUserHash = buf;
            g_useCatbox = SendMessage(GetDlgItem(hwnd, ID_CHECKBOX_USE_CATBOX), BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveSettings();
            DestroyWindow(hwnd);
        }
        break;
    case WM_DESTROY:
        g_hCatboxWnd = nullptr;
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
