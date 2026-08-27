#include "options_window.h"
#include "utils.h"
#include "ui_controls.h"
#include "ui_updates.h"
#include "timeline.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cwchar>

static HWND g_hOptionsWnd = nullptr;
static HWND g_hUploadWnd = nullptr;
static HWND g_hCatboxWnd = nullptr;
static HWND g_hGeneralPanel = nullptr;
static HWND g_hAudioPanel = nullptr;
static HWND g_hEncodingPanel = nullptr;
static HWND g_hUploadPanel = nullptr;
static HWND g_hExportPanel = nullptr;
static HWND g_hExportMasterGainLabel = nullptr;
static int g_selectedCategory = ID_TAB_GENERAL;
static HBRUSH g_hOptionsBgBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);

static LRESULT CALLBACK OptionsPanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void EnsurePanelClassRegistered(HINSTANCE hInst);

static HBRUSH ApplyDarkColors(HDC hdc, UINT msg)
{
    SetTextColor(hdc, RGB(240, 240, 240));
    SetBkColor(hdc, RGB(0, 0, 0));
    return g_hOptionsBgBrush;
}

// Global option variables
EncoderSelection g_encoderSelection = EncoderSelection::Libx264;
bool g_logToFile = true;
bool g_autoPlay = true;
bool g_showVideoPreviewOnHover = true;
bool g_improveSeekPerformance = true;
bool g_enableMultiClipEditing = false;
bool g_showAudioWaveform = true;
bool g_highlightSpeechWaveforms = false;
int g_exportMasterGainDb = 0;
std::wstring g_qualityPreset = L"Medium";
std::wstring g_b2KeyId;
std::wstring g_b2AppKey;
std::wstring g_b2BucketId;
std::wstring g_b2BucketName;
std::wstring g_b2CustomUrl;
bool g_autoUpload = false;
bool g_useCatbox = false;
bool g_useB2 = true;
std::wstring g_catboxUserHash;

// Exportation settings
std::wstring g_exportSaveName = L"$[filename]_edited";
std::wstring g_exportDefaultFolder;
bool g_exportAutoSave = false;
bool g_exportDefaultCodecH264 = true;

float GetExportMasterGainLinear()
{
    return std::pow(10.0f, g_exportMasterGainDb / 20.0f);
}

static void UpdateExportMasterGainLabel(int gainDb)
{
    if (!g_hExportMasterGainLabel)
        return;
    wchar_t text[32] = {};
    if (gainDb > 0)
        swprintf_s(text, L"+%d dB", gainDb);
    else
        swprintf_s(text, L"%d dB", gainDb);
    SetWindowTextW(g_hExportMasterGainLabel, text);
}

// Load settings from Windows registry
void LoadSettings()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\VideoEditor", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val; DWORD size = sizeof(val);
        if (RegQueryValueExW(hKey, L"EncoderType", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            if (val <= static_cast<DWORD>(EncoderSelection::Amf))
                g_encoderSelection = static_cast<EncoderSelection>(val);
        } else if (RegQueryValueExW(hKey, L"UseNvenc", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS) {
            g_encoderSelection = (val != 0) ? EncoderSelection::Nvenc : EncoderSelection::Libx264;
        }
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"EnableLogFile", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_logToFile = (val != 0);
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"AutoPlay", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_autoPlay = (val != 0);
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"HoverPreview", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_showVideoPreviewOnHover = (val != 0);
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"SeekPerformance", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_improveSeekPerformance = (val != 0);
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"EnableMultiClipEditing", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_enableMultiClipEditing = (val != 0);
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"ShowAudioWaveform", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_showAudioWaveform = (val != 0);
        size = sizeof(val); val = 0;
        if (RegQueryValueExW(hKey, L"HighlightSpeechWaveforms", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_highlightSpeechWaveforms = (val != 0);
        size = sizeof(val); val = 0;
        if (RegQueryValueExW(hKey, L"ExportMasterGainDb", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_exportMasterGainDb = std::clamp(static_cast<int32_t>(val), -12, 12);

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
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"QualityPreset", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_qualityPreset = buf;

        // Exportation settings
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"ExportSaveName", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_exportSaveName = buf;
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"ExportDefaultFolder", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_exportDefaultFolder = buf;
        sz = sizeof(DWORD); val = 0;
        if (RegQueryValueExW(hKey, L"ExportAutoSave", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            g_exportAutoSave = val != 0;
        sz = sizeof(DWORD); val = 1;
        if (RegQueryValueExW(hKey, L"ExportDefaultCodecH264", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            g_exportDefaultCodecH264 = val != 0;

        RegCloseKey(hKey);
    }
}

// Save settings to Windows registry
void SaveSettings()
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\VideoEditor", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD val = static_cast<DWORD>(g_encoderSelection);
        RegSetValueExW(hKey, L"EncoderType", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_encoderSelection == EncoderSelection::Nvenc ? 1 : 0;
        RegSetValueExW(hKey, L"UseNvenc", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_logToFile ? 1 : 0;
        RegSetValueExW(hKey, L"EnableLogFile", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_autoPlay ? 1 : 0;
        RegSetValueExW(hKey, L"AutoPlay", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_showVideoPreviewOnHover ? 1 : 0;
        RegSetValueExW(hKey, L"HoverPreview", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_improveSeekPerformance ? 1 : 0;
        RegSetValueExW(hKey, L"SeekPerformance", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_enableMultiClipEditing ? 1 : 0;
        RegSetValueExW(hKey, L"EnableMultiClipEditing", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_showAudioWaveform ? 1 : 0;
        RegSetValueExW(hKey, L"ShowAudioWaveform", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_highlightSpeechWaveforms ? 1 : 0;
        RegSetValueExW(hKey, L"HighlightSpeechWaveforms", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = static_cast<DWORD>(g_exportMasterGainDb);
        RegSetValueExW(hKey, L"ExportMasterGainDb", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
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
        RegSetValueExW(hKey, L"QualityPreset", 0, REG_SZ, (const BYTE*)g_qualityPreset.c_str(), (DWORD)((g_qualityPreset.size()+1)*sizeof(wchar_t)));

        // Exportation settings
        RegSetValueExW(hKey, L"ExportSaveName", 0, REG_SZ, (const BYTE*)g_exportSaveName.c_str(), (DWORD)((g_exportSaveName.size()+1)*sizeof(wchar_t)));
        RegSetValueExW(hKey, L"ExportDefaultFolder", 0, REG_SZ, (const BYTE*)g_exportDefaultFolder.c_str(), (DWORD)((g_exportDefaultFolder.size()+1)*sizeof(wchar_t)));
        val = g_exportAutoSave ? 1 : 0;
        RegSetValueExW(hKey, L"ExportAutoSave", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = g_exportDefaultCodecH264 ? 1 : 0;
        RegSetValueExW(hKey, L"ExportDefaultCodecH264", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        RegCloseKey(hKey);
    }
}

static void EnsurePanelClassRegistered(HINSTANCE hInst)
{
    static bool registered = false;
    if (registered)
        return;

    WNDCLASS wc = {};
    wc.lpfnWndProc = OptionsPanelProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"OptionsPanelClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&wc);
    registered = true;
}

static LRESULT CALLBACK OptionsPanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        return SendMessage(GetParent(hwnd), msg, wParam, lParam);
    case WM_HSCROLL:
        return SendMessage(GetParent(hwnd), msg, wParam, lParam);
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)ApplyDarkColors((HDC)wParam, msg);
    case WM_ERASEBKGND:
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wParam, &rc, g_hOptionsBgBrush);
            return 1;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Helper function to switch between category panels
void SwitchCategory(int categoryId)
{
    g_selectedCategory = categoryId;
    
    // Hide all panels
    if (g_hGeneralPanel) ShowWindow(g_hGeneralPanel, SW_HIDE);
    if (g_hAudioPanel) ShowWindow(g_hAudioPanel, SW_HIDE);
    if (g_hEncodingPanel) ShowWindow(g_hEncodingPanel, SW_HIDE);
    if (g_hUploadPanel) ShowWindow(g_hUploadPanel, SW_HIDE);
    if (g_hExportPanel) ShowWindow(g_hExportPanel, SW_HIDE);
    
    // Show selected panel
    if (categoryId == ID_TAB_GENERAL && g_hGeneralPanel)
        ShowWindow(g_hGeneralPanel, SW_SHOW);
    else if (categoryId == ID_TAB_AUDIO && g_hAudioPanel)
        ShowWindow(g_hAudioPanel, SW_SHOW);
    else if (categoryId == ID_TAB_ENCODING && g_hEncodingPanel)
        ShowWindow(g_hEncodingPanel, SW_SHOW);
    else if (categoryId == ID_TAB_UPLOAD && g_hUploadPanel)
        ShowWindow(g_hUploadPanel, SW_SHOW);
    else if (categoryId == ID_TAB_EXPORT && g_hExportPanel)
        ShowWindow(g_hExportPanel, SW_SHOW);
    
    // Force redraw of category buttons to update highlighting
    InvalidateRect(g_hOptionsWnd, nullptr, TRUE);
}

void ShowOptionsWindow(HWND parent)
{
    if (g_hOptionsWnd) {
        SetForegroundWindow(g_hOptionsWnd);
        return;
    }

    // Create larger, more professional window
    g_hOptionsWnd = CreateWindowEx(0, L"OptionsClass", L"Preferences",
                                   WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 720, 480,
                                   parent, nullptr,
                                   (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hOptionsWnd);
    CenterWindow(g_hOptionsWnd, parent);

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(g_hOptionsWnd, GWLP_HINSTANCE);
    EnsurePanelClassRegistered(hInst);
    int leftPanelWidth = 180;
    int contentX = leftPanelWidth + 25;
    int contentWidth = 720 - contentX - 25;
    int contentY = 20;

    // ===== LEFT PANEL - Category Navigation (DaVinci Resolve style) =====
    HWND hTabGeneral = CreateWindow(L"BUTTON", L"  General",
                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_LEFT,
                                    0, 50, leftPanelWidth, 40, g_hOptionsWnd,
                                    (HMENU)ID_TAB_GENERAL, hInst, nullptr);
    HWND hTabEncoding = CreateWindow(L"BUTTON", L"  Encoding",
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_LEFT,
                                     0, 130, leftPanelWidth, 40, g_hOptionsWnd,
                                     (HMENU)ID_TAB_ENCODING, hInst, nullptr);
    HWND hTabAudio = CreateWindow(L"BUTTON", L"  Audio",
                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_LEFT,
                                  0, 90, leftPanelWidth, 40, g_hOptionsWnd,
                                  (HMENU)ID_TAB_AUDIO, hInst, nullptr);
    HWND hTabUpload = CreateWindow(L"BUTTON", L"  Upload",
                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_LEFT,
                                   0, 170, leftPanelWidth, 40, g_hOptionsWnd,
                                   (HMENU)ID_TAB_UPLOAD, hInst, nullptr);
    HWND hTabExport = CreateWindow(L"BUTTON", L"  Exportation",
                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_LEFT,
                                   0, 210, leftPanelWidth, 40, g_hOptionsWnd,
                                   (HMENU)ID_TAB_EXPORT, hInst, nullptr);

    ApplyDarkTheme(hTabGeneral);
    ApplyDarkTheme(hTabAudio);
    ApplyDarkTheme(hTabEncoding);
    ApplyDarkTheme(hTabUpload);
    ApplyDarkTheme(hTabExport);

    // ===== GENERAL PANEL =====
    g_hGeneralPanel = CreateWindowEx(0, L"OptionsPanelClass", nullptr,
                                     WS_CHILD | WS_VISIBLE,
                                     contentX, contentY, contentWidth, 380,
                                     g_hOptionsWnd, (HMENU)ID_PANEL_GENERAL, hInst, nullptr);

    CreateWindow(L"STATIC", L"General Settings", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 0, contentWidth, 26, g_hGeneralPanel, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                 0, 32, contentWidth, 2, g_hGeneralPanel, nullptr, hInst, nullptr);

    CreateWindow(L"STATIC", L"Playback", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 50, contentWidth, 22, g_hGeneralPanel, nullptr, hInst, nullptr);
    HWND hAuto = CreateWindow(L"BUTTON", L"Auto-play after import",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              20, 78, 250, 22, g_hGeneralPanel,
                              (HMENU)ID_CHECKBOX_AUTO_PLAY, hInst, nullptr);
    ApplyDarkTheme(hAuto);
    HWND hHoverPreview = CreateWindow(L"BUTTON", L"Show video preview on hover",
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                      20, 106, 250, 22, g_hGeneralPanel,
                                      (HMENU)ID_CHECKBOX_HOVER_PREVIEW, hInst, nullptr);
    ApplyDarkTheme(hHoverPreview);

    HWND hSeekPerf = CreateWindow(L"BUTTON", L"Improve frame seek performance",
                                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                  20, 134, 250, 22, g_hGeneralPanel,
                                  (HMENU)ID_CHECKBOX_IMPROVE_SEEK, hInst, nullptr);
    ApplyDarkTheme(hSeekPerf);
    SendMessage(hSeekPerf, BM_SETCHECK, g_improveSeekPerformance ? BST_CHECKED : BST_UNCHECKED, 0);

    // Tooltip for seek performance
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icex);

    // Parent to NULL (top-level popup) to avoid message swallowed by child panels
    HWND hTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
                                   WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                   nullptr, nullptr, hInst, nullptr);
    
    SendMessage(hTooltip, TTM_SETMAXTIPWIDTH, 0, 300);

    TOOLINFO ti = { 0 };
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g_hGeneralPanel;
    ti.uId = (UINT_PTR)hSeekPerf;
    ti.lpszText = (LPWSTR)L"Increase the ram consumption to ~1.3 gb but improves the speed of frame seeking using the buttons ',' and '.'";
    
    SendMessage(hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    SendMessage(hTooltip, TTM_ACTIVATE, TRUE, 0);
    SendMessage(hTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);

    HWND hAudioWaveform = CreateWindow(L"BUTTON", L"Show audio waveform on timeline",
                                       WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                       20, 162, 300, 22, g_hGeneralPanel,
                                       (HMENU)ID_CHECKBOX_AUDIO_WAVEFORM, hInst, nullptr);
    ApplyDarkTheme(hAudioWaveform);

    CreateWindow(L"STATIC", L"Editing", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 199, contentWidth, 22, g_hGeneralPanel, nullptr, hInst, nullptr);
    HWND hMultiClip = CreateWindow(L"BUTTON", L"Enable multiple clip editing",
                                   WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                   20, 222, 250, 22, g_hGeneralPanel,
                                   (HMENU)ID_CHECKBOX_MULTI_CLIP, hInst, nullptr);
    ApplyDarkTheme(hMultiClip);

    CreateWindow(L"STATIC", L"Logging", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 259, contentWidth, 22, g_hGeneralPanel, nullptr, hInst, nullptr);
    HWND hLog = CreateWindow(L"BUTTON", L"Enable debug logging",
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             20, 282, 250, 22, g_hGeneralPanel,
                             (HMENU)ID_CHECKBOX_ENABLE_LOG, hInst, nullptr);
    ApplyDarkTheme(hLog);
    SendMessage(hLog, BM_SETCHECK, g_logToFile ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(hAuto, BM_SETCHECK, g_autoPlay ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(hHoverPreview, BM_SETCHECK, g_showVideoPreviewOnHover ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(hSeekPerf, BM_SETCHECK, g_improveSeekPerformance ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(hMultiClip, BM_SETCHECK, g_enableMultiClipEditing ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(hAudioWaveform, BM_SETCHECK, g_showAudioWaveform ? BST_CHECKED : BST_UNCHECKED, 0);

    // ===== AUDIO PANEL =====
    g_hAudioPanel = CreateWindowEx(0, L"OptionsPanelClass", nullptr,
                                   WS_CHILD,
                                   contentX, contentY, contentWidth, 380,
                                   g_hOptionsWnd, (HMENU)ID_PANEL_AUDIO, hInst, nullptr);

    CreateWindow(L"STATIC", L"Audio Settings", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 0, contentWidth, 26, g_hAudioPanel, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                 0, 32, contentWidth, 2, g_hAudioPanel, nullptr, hInst, nullptr);

    CreateWindow(L"STATIC", L"Timeline Waveforms", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 50, contentWidth, 22, g_hAudioPanel, nullptr, hInst, nullptr);
    HWND hHighlightSpeech = CreateWindow(
        L"BUTTON", L"Highlight speech in audio waveforms",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        20, 78, 330, 22, g_hAudioPanel,
        (HMENU)ID_CHECKBOX_HIGHLIGHT_SPEECH, hInst, nullptr);
    ApplyDarkTheme(hHighlightSpeech);
    SendMessage(hHighlightSpeech, BM_SETCHECK,
                g_highlightSpeechWaveforms ? BST_CHECKED : BST_UNCHECKED, 0);
    CreateWindow(
        L"STATIC",
        L"Uses neural voice detection. Keep disabled for faster waveform loading.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 106, contentWidth - 20, 38, g_hAudioPanel, nullptr, hInst, nullptr);

    CreateWindow(L"STATIC", L"Export Audio", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 160, contentWidth, 22, g_hAudioPanel, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"Export master gain:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 20, 190, 150, 22, g_hAudioPanel, nullptr, hInst, nullptr);
    HWND hExportGain = CreateWindow(
        TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ,
        20, 216, 340, 36, g_hAudioPanel,
        (HMENU)ID_SLIDER_EXPORT_MASTER_GAIN, hInst, nullptr);
    SendMessage(hExportGain, TBM_SETRANGE, TRUE, MAKELPARAM(-12, 12));
    SendMessage(hExportGain, TBM_SETTICFREQ, 1, 0);
    SendMessage(hExportGain, TBM_SETPOS, TRUE, g_exportMasterGainDb);
    g_hExportMasterGainLabel = CreateWindow(
        L"STATIC", L"0 dB", WS_CHILD | WS_VISIBLE | SS_CENTER,
        370, 220, 70, 22, g_hAudioPanel,
        (HMENU)ID_LABEL_EXPORT_MASTER_GAIN, hInst, nullptr);
    UpdateExportMasterGainLabel(g_exportMasterGainDb);
    CreateWindow(
        L"STATIC",
        L"Added to every exported audio track. 0 dB keeps the original level.",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 260, contentWidth - 20, 38, g_hAudioPanel, nullptr, hInst, nullptr);

    // ===== ENCODING PANEL =====
    g_hEncodingPanel = CreateWindowEx(0, L"OptionsPanelClass", nullptr,
                                      WS_CHILD,
                                      contentX, contentY, contentWidth, 380,
                                      g_hOptionsWnd, (HMENU)ID_PANEL_ENCODING, hInst, nullptr);

    CreateWindow(L"STATIC", L"Encoding Settings", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 0, contentWidth, 26, g_hEncodingPanel, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                 0, 32, contentWidth, 2, g_hEncodingPanel, nullptr, hInst, nullptr);

    CreateWindow(L"STATIC", L"Video Codec", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 50, contentWidth, 22, g_hEncodingPanel, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"Encoder:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 20, 80, 120, 22, g_hEncodingPanel, nullptr, hInst, nullptr);
    HWND hEncoderCombo = CreateWindow(L"COMBOBOX", L"",
                                      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                      150, 78, 280, 200, g_hEncodingPanel,
                                      (HMENU)ID_COMBO_ENCODER, hInst, nullptr);
    ApplyDarkTheme(hEncoderCombo);
    SendMessage(hEncoderCombo, CB_ADDSTRING, 0, (LPARAM)L"H.264 (libx264) - Software");
    SendMessage(hEncoderCombo, CB_ADDSTRING, 0, (LPARAM)L"H.264 (NVENC) - NVIDIA GPU");
    SendMessage(hEncoderCombo, CB_ADDSTRING, 0, (LPARAM)L"H.264 (AMF) - AMD GPU");
    SendMessage(hEncoderCombo, CB_SETCURSEL, static_cast<int>(g_encoderSelection), 0);

    CreateWindow(L"STATIC", L"Quality:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 20, 115, 120, 22, g_hEncodingPanel, nullptr, hInst, nullptr);
    HWND hQualityCombo = CreateWindow(L"COMBOBOX", L"",
                                      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                      150, 113, 280, 200, g_hEncodingPanel,
                                      (HMENU)ID_COMBO_QUALITY, hInst, nullptr);
    ApplyDarkTheme(hQualityCombo);
    SendMessage(hQualityCombo, CB_ADDSTRING, 0, (LPARAM)L"Low");
    SendMessage(hQualityCombo, CB_ADDSTRING, 0, (LPARAM)L"Medium");
    SendMessage(hQualityCombo, CB_ADDSTRING, 0, (LPARAM)L"High");
    SendMessage(hQualityCombo, CB_ADDSTRING, 0, (LPARAM)L"Very High");
    int qualIdx = 1;
    if (g_qualityPreset == L"Low") qualIdx = 0;
    else if (g_qualityPreset == L"Medium") qualIdx = 1;
    else if (g_qualityPreset == L"High") qualIdx = 2;
    else if (g_qualityPreset == L"Very High") qualIdx = 3;
    SendMessage(hQualityCombo, CB_SETCURSEL, qualIdx, 0);

    // ===== UPLOAD PANEL =====
    g_hUploadPanel = CreateWindowEx(0, L"OptionsPanelClass", nullptr,
                                    WS_CHILD,
                                    contentX, contentY, contentWidth, 380,
                                    g_hOptionsWnd, (HMENU)ID_PANEL_UPLOAD, hInst, nullptr);

    CreateWindow(L"STATIC", L"Upload Settings", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 0, contentWidth, 26, g_hUploadPanel, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                 0, 32, contentWidth, 2, g_hUploadPanel, nullptr, hInst, nullptr);

    CreateWindow(L"STATIC", L"Auto Upload", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 50, contentWidth, 22, g_hUploadPanel, nullptr, hInst, nullptr);
    HWND hAutoUpload = CreateWindow(L"BUTTON", L"Automatically upload after export",
                                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                    20, 78, 300, 22, g_hUploadPanel,
                                    (HMENU)ID_CHECKBOX_AUTO_UPLOAD, hInst, nullptr);
    ApplyDarkTheme(hAutoUpload);
    SendMessage(hAutoUpload, BM_SETCHECK, g_autoUpload ? BST_CHECKED : BST_UNCHECKED, 0);

    CreateWindow(L"STATIC", L"Upload Services", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 115, contentWidth, 22, g_hUploadPanel, nullptr, hInst, nullptr);

    HWND hCatbox = CreateWindow(L"BUTTON", L"Catbox.moe Settings",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                20, 145, 200, 32, g_hUploadPanel,
                                (HMENU)ID_BUTTON_CATBOX_CONFIG, hInst, nullptr);

    HWND hB2 = CreateWindow(L"BUTTON", L"Backblaze B2 Settings",
                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            240, 145, 200, 32, g_hUploadPanel,
                            (HMENU)ID_BUTTON_B2_SETTINGS, hInst, nullptr);

    ApplyDarkTheme(hCatbox);
    ApplyDarkTheme(hB2);

    // ===== EXPORTATION PANEL =====
    g_hExportPanel = CreateWindowEx(0, L"OptionsPanelClass", nullptr,
                                    WS_CHILD,
                                    contentX, contentY, contentWidth, 380,
                                    g_hOptionsWnd, (HMENU)ID_PANEL_EXPORT, hInst, nullptr);

    CreateWindow(L"STATIC", L"Exportation Settings", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 0, contentWidth, 26, g_hExportPanel, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                 0, 32, contentWidth, 2, g_hExportPanel, nullptr, hInst, nullptr);

    CreateWindow(L"STATIC", L"Save Name", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 50, contentWidth, 22, g_hExportPanel, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"Template:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 20, 78, 80, 22, g_hExportPanel, nullptr, hInst, nullptr);
    HWND hExportName = CreateWindow(L"EDIT", g_exportSaveName.c_str(),
                                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                    110, 76, 350, 22, g_hExportPanel,
                                    (HMENU)ID_EDIT_EXPORT_NAME, hInst, nullptr);
    ApplyDarkTheme(hExportName);
    CreateWindow(L"STATIC", L"Use $[filename] for the original file name (without extension).",
                 WS_CHILD | WS_VISIBLE | SS_LEFT,
                 20, 104, contentWidth - 20, 22, g_hExportPanel, nullptr, hInst, nullptr);

    CreateWindow(L"STATIC", L"Default Folder Location", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 140, contentWidth, 22, g_hExportPanel, nullptr, hInst, nullptr);
    HWND hExportFolder = CreateWindow(L"EDIT", g_exportDefaultFolder.c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
                                      20, 168, 280, 22, g_hExportPanel,
                                      (HMENU)(ID_EDIT_EXPORT_NAME + 100), hInst, nullptr);
    ApplyDarkTheme(hExportFolder);
    HWND hBrowse = CreateWindow(L"BUTTON", L"Browse...",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                310, 166, 70, 26, g_hExportPanel,
                                (HMENU)ID_BUTTON_EXPORT_FOLDER, hInst, nullptr);
    ApplyDarkTheme(hBrowse);
    HWND hClearFolder = CreateWindow(L"BUTTON", L"Clear",
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     390, 166, 55, 26, g_hExportPanel,
                                     (HMENU)ID_BUTTON_EXPORT_FOLDER_CLEAR, hInst, nullptr);
    ApplyDarkTheme(hClearFolder);
    CreateWindow(L"STATIC", L"If empty, defaults to the folder of the imported file.",
                 WS_CHILD | WS_VISIBLE | SS_LEFT,
                 20, 196, contentWidth - 20, 22, g_hExportPanel, nullptr, hInst, nullptr);

    CreateWindow(L"STATIC", L"Quick Export", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 232, contentWidth, 22, g_hExportPanel, nullptr, hInst, nullptr);
    HWND hAutoExport = CreateWindow(L"BUTTON", L"Automatically save without asking name and path",
                                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                    20, 260, 420, 22, g_hExportPanel,
                                    (HMENU)ID_CHECKBOX_AUTO_EXPORT, hInst, nullptr);
    ApplyDarkTheme(hAutoExport);
    SendMessage(hAutoExport, BM_SETCHECK, g_exportAutoSave ? BST_CHECKED : BST_UNCHECKED, 0);

    CreateWindow(L"STATIC", L"Default Codec", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 0, 298, contentWidth, 22, g_hExportPanel, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"Codec:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 20, 326, 80, 22, g_hExportPanel, nullptr, hInst, nullptr);
    HWND hCodecCombo = CreateWindow(L"COMBOBOX", L"",
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                    110, 324, 200, 200, g_hExportPanel,
                                    (HMENU)ID_COMBO_DEFAULT_CODEC, hInst, nullptr);
    ApplyDarkTheme(hCodecCombo);
    SendMessage(hCodecCombo, CB_ADDSTRING, 0, (LPARAM)L"H264 (re-encode)");
    SendMessage(hCodecCombo, CB_ADDSTRING, 0, (LPARAM)L"Copy codec (no re-encode)");
    SendMessage(hCodecCombo, CB_SETCURSEL, g_exportDefaultCodecH264 ? 0 : 1, 0);

    // ===== BOTTOM BUTTONS =====
    HWND hOk = CreateWindow(L"BUTTON", L"OK",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            500, 400, 90, 32, g_hOptionsWnd,
                            (HMENU)IDOK, hInst, nullptr);
    HWND hCancel = CreateWindow(L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                600, 400, 90, 32, g_hOptionsWnd,
                                (HMENU)IDCANCEL, hInst, nullptr);
    ApplyDarkTheme(hOk);
    ApplyDarkTheme(hCancel);

    // Show General panel by default
    g_selectedCategory = ID_TAB_GENERAL;
    SwitchCategory(ID_TAB_GENERAL);
}

LRESULT CALLBACK OptionsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)ApplyDarkColors((HDC)wParam, msg);
    case WM_ERASEBKGND:
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wParam, &rc, g_hOptionsBgBrush);
            return 1;
        }
    case WM_COMMAND:
        // Handle category button clicks
        if (LOWORD(wParam) == ID_TAB_GENERAL || 
            LOWORD(wParam) == ID_TAB_AUDIO ||
            LOWORD(wParam) == ID_TAB_ENCODING || 
            LOWORD(wParam) == ID_TAB_UPLOAD ||
            LOWORD(wParam) == ID_TAB_EXPORT) {
            SwitchCategory(LOWORD(wParam));
        }
        else if (LOWORD(wParam) == ID_BUTTON_EXPORT_FOLDER) {
            IFileOpenDialog* pfd = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
                DWORD options = 0;
                pfd->GetOptions(&options);
                pfd->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                pfd->SetTitle(L"Select default export folder");
                if (SUCCEEDED(pfd->Show(hwnd))) {
                    IShellItem* psi = nullptr;
                    if (SUCCEEDED(pfd->GetResult(&psi))) {
                        PWSTR folderPath = nullptr;
                        if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &folderPath))) {
                            g_exportDefaultFolder = folderPath;
                            HWND hFolderEdit = GetDlgItem(g_hExportPanel, ID_EDIT_EXPORT_NAME + 100);
                            if (hFolderEdit)
                                SetWindowTextW(hFolderEdit, folderPath);
                            CoTaskMemFree(folderPath);
                        }
                        psi->Release();
                    }
                }
                pfd->Release();
            }
        }
        else if (LOWORD(wParam) == ID_BUTTON_EXPORT_FOLDER_CLEAR) {
            g_exportDefaultFolder.clear();
            HWND hFolderEdit = GetDlgItem(g_hExportPanel, ID_EDIT_EXPORT_NAME + 100);
            if (hFolderEdit)
                SetWindowTextW(hFolderEdit, L"");
        }
        else if (LOWORD(wParam) == ID_BUTTON_CATBOX_CONFIG) {
            ShowCatboxConfigWindow(hwnd);
        }
        else if (LOWORD(wParam) == ID_BUTTON_B2_SETTINGS) {
            ShowB2ConfigWindow(hwnd);
        } else if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            // Get encoder selection from combo box
            HWND hEncoderCombo = GetDlgItem(g_hEncodingPanel, ID_COMBO_ENCODER);
            int encoderIdx = (int)SendMessage(hEncoderCombo, CB_GETCURSEL, 0, 0);
            if (encoderIdx >= 0 && encoderIdx <= 2) {
                g_encoderSelection = static_cast<EncoderSelection>(encoderIdx);
            }

            // Get quality preset from combo box
            HWND hQualityCombo = GetDlgItem(g_hEncodingPanel, ID_COMBO_QUALITY);
            int qualityIdx = (int)SendMessage(hQualityCombo, CB_GETCURSEL, 0, 0);
            switch (qualityIdx) {
                case 0: g_qualityPreset = L"Low"; break;
                case 1: g_qualityPreset = L"Medium"; break;
                case 2: g_qualityPreset = L"High"; break;
                case 3: g_qualityPreset = L"Very High"; break;
                default: g_qualityPreset = L"Medium"; break;
            }

            // Get checkbox states
            HWND hLog = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_ENABLE_LOG);
            HWND hAuto = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_AUTO_PLAY);
            HWND hHoverPrev = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_HOVER_PREVIEW);
            HWND hSeekPerf = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_IMPROVE_SEEK);
            HWND hMultiClip = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_MULTI_CLIP);
            HWND hAudioWaveform = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_AUDIO_WAVEFORM);
            HWND hHighlightSpeech = GetDlgItem(g_hAudioPanel, ID_CHECKBOX_HIGHLIGHT_SPEECH);
            HWND hExportGain = GetDlgItem(g_hAudioPanel, ID_SLIDER_EXPORT_MASTER_GAIN);
            HWND hAutoUpload = GetDlgItem(g_hUploadPanel, ID_CHECKBOX_AUTO_UPLOAD);
            const bool previousShowAudioWaveform = g_showAudioWaveform;
            const bool previousHighlightSpeech = g_highlightSpeechWaveforms;
            g_logToFile = SendMessage(hLog, BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_autoPlay = SendMessage(hAuto, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hHoverPrev) g_showVideoPreviewOnHover = SendMessage(hHoverPrev, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hSeekPerf) g_improveSeekPerformance = SendMessage(hSeekPerf, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hMultiClip) g_enableMultiClipEditing = SendMessage(hMultiClip, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hAudioWaveform) g_showAudioWaveform = SendMessage(hAudioWaveform, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hHighlightSpeech) g_highlightSpeechWaveforms = SendMessage(hHighlightSpeech, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hExportGain) g_exportMasterGainDb = std::clamp(
                static_cast<int>(SendMessage(hExportGain, TBM_GETPOS, 0, 0)),
                -12, 12);
            g_autoUpload = SendMessage(hAutoUpload, BM_GETCHECK, 0, 0) == BST_CHECKED;

            // Get exportation settings
            {
                wchar_t nameBuf[512] = {};
                HWND hExpName = GetDlgItem(g_hExportPanel, ID_EDIT_EXPORT_NAME);
                if (hExpName) {
                    GetWindowTextW(hExpName, nameBuf, 512);
                    g_exportSaveName = nameBuf;
                }
                HWND hAutoExp = GetDlgItem(g_hExportPanel, ID_CHECKBOX_AUTO_EXPORT);
                if (hAutoExp)
                    g_exportAutoSave = SendMessage(hAutoExp, BM_GETCHECK, 0, 0) == BST_CHECKED;
                HWND hCodecCombo = GetDlgItem(g_hExportPanel, ID_COMBO_DEFAULT_CODEC);
                if (hCodecCombo)
                    g_exportDefaultCodecH264 = SendMessage(hCodecCombo, CB_GETCURSEL, 0, 0) == 0;
            }

            SaveSettings();
            if (previousShowAudioWaveform != g_showAudioWaveform ||
                previousHighlightSpeech != g_highlightSpeechWaveforms)
            {
                RefreshAudioWaveformPreview();
            }
            HWND owner = GetWindow(hwnd, GW_OWNER);
            DestroyWindow(hwnd);
            if (owner) {
                RepositionControls(owner);
                UpdateCutInfoLabel(owner);
                InvalidateRect(owner, nullptr, TRUE);
            }
        }
        break;
    case WM_HSCROLL:
        if ((HWND)lParam &&
            GetDlgCtrlID((HWND)lParam) == ID_SLIDER_EXPORT_MASTER_GAIN)
        {
            UpdateExportMasterGainLabel(static_cast<int>(
                SendMessage((HWND)lParam, TBM_GETPOS, 0, 0)));
        }
        break;
    case WM_CLOSE:
        {
            // Get encoder selection from combo box
            HWND hEncoderCombo = GetDlgItem(g_hEncodingPanel, ID_COMBO_ENCODER);
            int encoderIdx = (int)SendMessage(hEncoderCombo, CB_GETCURSEL, 0, 0);
            if (encoderIdx >= 0 && encoderIdx <= 2) {
                g_encoderSelection = static_cast<EncoderSelection>(encoderIdx);
            }

            // Get quality preset from combo box
            HWND hQualityCombo = GetDlgItem(g_hEncodingPanel, ID_COMBO_QUALITY);
            int qualityIdx = (int)SendMessage(hQualityCombo, CB_GETCURSEL, 0, 0);
            switch (qualityIdx) {
                case 0: g_qualityPreset = L"Low"; break;
                case 1: g_qualityPreset = L"Medium"; break;
                case 2: g_qualityPreset = L"High"; break;
                case 3: g_qualityPreset = L"Very High"; break;
                default: g_qualityPreset = L"Medium"; break;
            }

            // Get checkbox states
            HWND hLog = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_ENABLE_LOG);
            HWND hAuto = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_AUTO_PLAY);
            HWND hHoverPrev = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_HOVER_PREVIEW);
            HWND hSeekPerf = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_IMPROVE_SEEK);
            HWND hMultiClip = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_MULTI_CLIP);
            HWND hAudioWaveform = GetDlgItem(g_hGeneralPanel, ID_CHECKBOX_AUDIO_WAVEFORM);
            HWND hHighlightSpeech = GetDlgItem(g_hAudioPanel, ID_CHECKBOX_HIGHLIGHT_SPEECH);
            HWND hExportGain = GetDlgItem(g_hAudioPanel, ID_SLIDER_EXPORT_MASTER_GAIN);
            const bool previousShowAudioWaveform = g_showAudioWaveform;
            const bool previousHighlightSpeech = g_highlightSpeechWaveforms;
            g_logToFile = SendMessage(hLog, BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_autoPlay = SendMessage(hAuto, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hHoverPrev) g_showVideoPreviewOnHover = SendMessage(hHoverPrev, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hSeekPerf) g_improveSeekPerformance = SendMessage(hSeekPerf, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hMultiClip) g_enableMultiClipEditing = SendMessage(hMultiClip, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hAudioWaveform) g_showAudioWaveform = SendMessage(hAudioWaveform, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hHighlightSpeech) g_highlightSpeechWaveforms = SendMessage(hHighlightSpeech, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hExportGain) g_exportMasterGainDb = std::clamp(
                static_cast<int>(SendMessage(hExportGain, TBM_GETPOS, 0, 0)),
                -12, 12);

            // Get exportation settings
            {
                wchar_t nameBuf[512] = {};
                HWND hExpName = GetDlgItem(g_hExportPanel, ID_EDIT_EXPORT_NAME);
                if (hExpName) {
                    GetWindowTextW(hExpName, nameBuf, 512);
                    g_exportSaveName = nameBuf;
                }
                HWND hAutoExp = GetDlgItem(g_hExportPanel, ID_CHECKBOX_AUTO_EXPORT);
                if (hAutoExp)
                    g_exportAutoSave = SendMessage(hAutoExp, BM_GETCHECK, 0, 0) == BST_CHECKED;
                HWND hCodecCombo = GetDlgItem(g_hExportPanel, ID_COMBO_DEFAULT_CODEC);
                if (hCodecCombo)
                    g_exportDefaultCodecH264 = SendMessage(hCodecCombo, CB_GETCURSEL, 0, 0) == 0;
            }

            SaveSettings();
            if (previousShowAudioWaveform != g_showAudioWaveform ||
                previousHighlightSpeech != g_highlightSpeechWaveforms)
            {
                RefreshAudioWaveformPreview();
            }
            HWND owner = GetWindow(hwnd, GW_OWNER);
            DestroyWindow(hwnd);
            if (owner) {
                RepositionControls(owner);
                UpdateCutInfoLabel(owner);
                InvalidateRect(owner, nullptr, TRUE);
            }
        }
        break;
    case WM_DESTROY:
        g_hOptionsWnd = nullptr;
        g_hGeneralPanel = nullptr;
        g_hAudioPanel = nullptr;
        g_hEncodingPanel = nullptr;
        g_hUploadPanel = nullptr;
        g_hExportPanel = nullptr;
        g_hExportMasterGainLabel = nullptr;
        g_selectedCategory = ID_TAB_GENERAL;
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------- B2 Configuration Window -----------------

static HWND g_hB2Wnd = nullptr;

void ShowB2ConfigWindow(HWND parent)
{
    if (g_hB2Wnd) { SetForegroundWindow(g_hB2Wnd); return; }

    g_hB2Wnd = CreateWindowEx(0, L"B2ConfigClass", L"Backblaze B2 Configuration",
                              WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_VSCROLL,
                              CW_USEDEFAULT, CW_USEDEFAULT, 650, 470,
                              parent, nullptr,
                              (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hB2Wnd);
    CenterWindow(g_hB2Wnd, parent);

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(g_hB2Wnd, GWLP_HINSTANCE);
    int labelWidth = 130;
    int inputX = labelWidth + 15;
    int inputWidth = 380;

    // Header
    CreateWindow(L"STATIC", L"Backblaze B2 Settings", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 15, 15, 250, 22, g_hB2Wnd, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                 15, 42, 550, 2, g_hB2Wnd, nullptr, hInst, nullptr);

    // Enable B2 checkbox
    HWND hEnableB2 = CreateWindow(L"BUTTON", L"Enable Backblaze B2 Upload", 
                                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                  15, 55, 250, 20, g_hB2Wnd,
                                  (HMENU)ID_CHECKBOX_USE_B2, hInst, nullptr);
    ApplyDarkTheme(hEnableB2);

    // Credentials section
    CreateWindow(L"STATIC", L"Credentials", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 15, 88, labelWidth, 18, g_hB2Wnd, nullptr, hInst, nullptr);

    CreateWindow(L"STATIC", L"Key ID:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                 15, 115, labelWidth, 20, g_hB2Wnd, nullptr, hInst, nullptr);
    HWND hKeyId = CreateWindow(L"EDIT", g_b2KeyId.c_str(),
                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                               inputX, 113, inputWidth, 22, g_hB2Wnd,
                               (HMENU)ID_EDIT_B2_KEY_ID, hInst, nullptr);

    CreateWindow(L"STATIC", L"Application Key:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                 15, 145, labelWidth, 20, g_hB2Wnd, nullptr, hInst, nullptr);
    HWND hAppKey = CreateWindow(L"EDIT", g_b2AppKey.c_str(),
                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
                               inputX, 143, inputWidth, 22, g_hB2Wnd,
                               (HMENU)ID_EDIT_B2_APP_KEY, hInst, nullptr);

    // Bucket section
    CreateWindow(L"STATIC", L"Bucket Information", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 15, 178, labelWidth, 18, g_hB2Wnd, nullptr, hInst, nullptr);

    CreateWindow(L"STATIC", L"Bucket ID:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                 15, 205, labelWidth, 20, g_hB2Wnd, nullptr, hInst, nullptr);
    HWND hBucketId = CreateWindow(L"EDIT", g_b2BucketId.c_str(),
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                 inputX, 203, inputWidth, 22, g_hB2Wnd,
                                 (HMENU)ID_EDIT_B2_BUCKET_ID, hInst, nullptr);

    CreateWindow(L"STATIC", L"Bucket Name:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                 15, 235, labelWidth, 20, g_hB2Wnd, nullptr, hInst, nullptr);
    HWND hBucketName = CreateWindow(L"EDIT", g_b2BucketName.c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                   inputX, 233, inputWidth, 22, g_hB2Wnd,
                                   (HMENU)ID_EDIT_B2_BUCKET_NAME, hInst, nullptr);

    CreateWindow(L"STATIC", L"Custom URL:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                 15, 265, labelWidth, 20, g_hB2Wnd, nullptr, hInst, nullptr);
    HWND hCustomUrl = CreateWindow(L"EDIT", g_b2CustomUrl.c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                  inputX, 263, inputWidth, 22, g_hB2Wnd,
                                  (HMENU)ID_EDIT_B2_CUSTOM_URL, hInst, nullptr);

    HWND hCustomUrlDesc = CreateWindow(L"STATIC",
                                       L"Optional custom base URL or CDN domain (e.g. https://media.example.com).\r\n"
                                       L"If set: creates links like https://media.example.com/video.mp4\r\n"
                                       L"If empty: defaults to https://<downloadUrl>/file/<bucket-name>/video.mp4",
                                       WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       inputX, 290, 480, 68, g_hB2Wnd, nullptr, hInst, nullptr);

    ApplyDarkTheme(hKeyId);
    ApplyDarkTheme(hAppKey);
    ApplyDarkTheme(hBucketId);
    ApplyDarkTheme(hBucketName);
    ApplyDarkTheme(hCustomUrl);
    ApplyDarkTheme(hCustomUrlDesc);

    // Bottom buttons
    HWND hOk = CreateWindow(L"BUTTON", L"OK",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            300, 375, 85, 28, g_hB2Wnd,
                            (HMENU)IDOK, hInst, nullptr);
    HWND hCancel = CreateWindow(L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                395, 375, 85, 28, g_hB2Wnd,
                                (HMENU)IDCANCEL, hInst, nullptr);

    ApplyDarkTheme(hOk);
    ApplyDarkTheme(hCancel);
    SendMessage(hEnableB2, BM_SETCHECK, g_useB2 ? BST_CHECKED : BST_UNCHECKED, 0);
}

LRESULT CALLBACK B2ConfigProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)ApplyDarkColors((HDC)wParam, msg);
    case WM_ERASEBKGND:
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wParam, &rc, g_hOptionsBgBrush);
            return 1;
        }
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

    g_hUploadWnd = CreateWindowEx(0, L"UploadConfigClass", L"Upload Services",
                                  WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 420, 200,
                                  parent, nullptr,
                                  (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hUploadWnd);
    CenterWindow(g_hUploadWnd, parent);

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(g_hUploadWnd, GWLP_HINSTANCE);

    // Header
    CreateWindow(L"STATIC", L"Upload Configuration", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 15, 15, 200, 22, g_hUploadWnd, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                 15, 42, 380, 2, g_hUploadWnd, nullptr, hInst, nullptr);

    // Auto upload option
    HWND hAuto = CreateWindow(L"BUTTON", L"Automatically upload after export",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              15, 55, 250, 20, g_hUploadWnd,
                              (HMENU)ID_CHECKBOX_AUTO_UPLOAD, hInst, nullptr);
    ApplyDarkTheme(hAuto);

    // Service configuration buttons
    CreateWindow(L"STATIC", L"Configure Services:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 15, 90, 150, 18, g_hUploadWnd, nullptr, hInst, nullptr);

    HWND hCatbox = CreateWindow(L"BUTTON", L"Catbox.moe Settings",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                15, 115, 180, 30, g_hUploadWnd,
                                (HMENU)ID_BUTTON_CATBOX_CONFIG, hInst, nullptr);

    HWND hB2 = CreateWindow(L"BUTTON", L"Backblaze B2 Settings",
                             WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             210, 115, 180, 30, g_hUploadWnd,
                             (HMENU)ID_BUTTON_B2_SETTINGS, hInst, nullptr);

    ApplyDarkTheme(hCatbox);
    ApplyDarkTheme(hB2);

    // Bottom buttons
    HWND hOk = CreateWindow(L"BUTTON", L"OK",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            220, 155, 85, 28, g_hUploadWnd,
                            (HMENU)IDOK, hInst, nullptr);
    HWND hCancel = CreateWindow(L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                315, 155, 85, 28, g_hUploadWnd,
                                (HMENU)IDCANCEL, hInst, nullptr);

    ApplyDarkTheme(hOk);
    ApplyDarkTheme(hCancel);

    SendMessage(hAuto, BM_SETCHECK, g_autoUpload ? BST_CHECKED : BST_UNCHECKED, 0);
}

LRESULT CALLBACK UploadProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)ApplyDarkColors((HDC)wParam, msg);
    case WM_ERASEBKGND:
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wParam, &rc, g_hOptionsBgBrush);
            return 1;
        }
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

    g_hCatboxWnd = CreateWindowEx(0, L"CatboxConfigClass", L"Catbox Configuration",
                                  WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE | WS_THICKFRAME | WS_MAXIMIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 550, 250,
                                  parent, nullptr,
                                  (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hCatboxWnd);
    CenterWindow(g_hCatboxWnd, parent);

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(g_hCatboxWnd, GWLP_HINSTANCE);
    int labelWidth = 100;
    int inputX = labelWidth + 15;

    // Header
    CreateWindow(L"STATIC", L"Catbox.moe Settings", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 15, 15, 200, 22, g_hCatboxWnd, nullptr, hInst, nullptr);
    CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                 15, 42, 470, 2, g_hCatboxWnd, nullptr, hInst, nullptr);

    // Enable checkbox
    HWND hEnable = CreateWindow(L"BUTTON", L"Enable Catbox.moe Upload", 
                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                15, 55, 250, 20, g_hCatboxWnd,
                                (HMENU)ID_CHECKBOX_USE_CATBOX, hInst, nullptr);
    ApplyDarkTheme(hEnable);

    // User hash field
    CreateWindow(L"STATIC", L"User Hash:", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                 15, 88, labelWidth, 20, g_hCatboxWnd, nullptr, hInst, nullptr);
    HWND hHash = CreateWindow(L"EDIT", g_catboxUserHash.c_str(),
                              WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                              inputX, 86, 330, 22, g_hCatboxWnd,
                              (HMENU)ID_EDIT_CATBOX_HASH, hInst, nullptr);
    ApplyDarkTheme(hHash);

    CreateWindow(L"STATIC", L"(Optional - for authenticated uploads)", 
                 WS_CHILD | WS_VISIBLE | SS_LEFT,
                 inputX, 112, 330, 16, g_hCatboxWnd, nullptr, hInst, nullptr);

    // Bottom buttons
    HWND hOk = CreateWindow(L"BUTTON", L"OK", 
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            200, 150, 85, 28, g_hCatboxWnd, 
                            (HMENU)IDOK, hInst, nullptr);
    HWND hCancel = CreateWindow(L"BUTTON", L"Cancel", 
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                375, 150, 85, 28, g_hCatboxWnd, 
                                (HMENU)IDCANCEL, hInst, nullptr);

    ApplyDarkTheme(hOk);
    ApplyDarkTheme(hCancel);

    SendMessage(hEnable, BM_SETCHECK, g_useCatbox ? BST_CHECKED : BST_UNCHECKED, 0);
}

LRESULT CALLBACK CatboxConfigProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)ApplyDarkColors((HDC)wParam, msg);
    case WM_ERASEBKGND:
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wParam, &rc, g_hOptionsBgBrush);
            return 1;
        }
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
