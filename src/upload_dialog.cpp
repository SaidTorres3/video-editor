#include "upload_dialog.h"
#include "utils.h"
#include "catbox_upload.h"
#include "b2_upload.h"
#include "editing.h"
#include "options_window.h"
#include <commctrl.h>
#include <shellapi.h>
#include <thread>

static HWND g_hUrlDlg = nullptr;
static std::wstring g_url1;
static std::wstring g_url2;

static HWND g_hManualUploadDlg = nullptr;
static std::wstring g_manualUploadPath;

static HWND g_hCatboxButton = nullptr;
static HWND g_hCatboxProgress = nullptr;
static HWND g_hCatboxUrl = nullptr;
static HWND g_hCatboxCopy = nullptr;
static HWND g_hCatboxStatus = nullptr;

static HWND g_hB2Button = nullptr;
static HWND g_hB2Progress = nullptr;
static HWND g_hB2Url = nullptr;
static HWND g_hB2Copy = nullptr;
static HWND g_hB2Status = nullptr;

static HWND g_hManualClose = nullptr;
static HWND g_hOpenFolder = nullptr;

static bool g_catboxUploading = false;
static bool g_b2Uploading = false;

enum class ManualUploadTarget { Catbox = 1, B2 = 2 };

struct ManualUploadResult {
    ManualUploadTarget target;
    bool success;
    std::wstring url;
};

constexpr UINT WM_MANUAL_UPLOAD_DONE = WM_APP + 2;
constexpr int ID_BTN_CATBOX_UPLOAD = 4101;
constexpr int ID_BTN_B2_UPLOAD = 4102;
constexpr int ID_BTN_CATBOX_COPY = 4103;
constexpr int ID_BTN_B2_COPY = 4104;
constexpr int ID_BTN_MANUAL_CLOSE = 4105;
constexpr int ID_BTN_OPEN_FOLDER = 4106;

static void CopyTextToClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty())
        return;
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t sz = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
        if (hMem) {
            memcpy(GlobalLock(hMem), text.c_str(), sz);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
}

void ShowUrlCopyDialog(HWND parent, const std::wstring& message, const std::wstring& url1, const std::wstring& url2) {
    g_url1 = url1;
    g_url2 = url2;
    if (g_hUrlDlg) DestroyWindow(g_hUrlDlg);
    int height = url2.empty() ? 160 : 200;
    g_hUrlDlg = CreateWindowEx(0, L"UrlCopyClass", L"Upload Complete",
                               WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
                               CW_USEDEFAULT, CW_USEDEFAULT, 360, height,
                               parent, nullptr,
                               (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    if (!g_hUrlDlg) return;
    ApplyDarkTheme(g_hUrlDlg);
    CenterWindow(g_hUrlDlg, parent);
    CreateWindow(L"STATIC", message.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                 10, 10, 330, 40, g_hUrlDlg, nullptr,
                 (HINSTANCE)GetWindowLongPtr(g_hUrlDlg, GWLP_HINSTANCE), nullptr);
    if (url2.empty()) {
        HWND hEdit = CreateWindow(L"EDIT", url1.c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
                                  10, 55, 330, 20, g_hUrlDlg, (HMENU)1,
                                  (HINSTANCE)GetWindowLongPtr(g_hUrlDlg, GWLP_HINSTANCE), nullptr);
        HWND hCopy = CreateWindow(L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  70, 100, 80, 25, g_hUrlDlg, (HMENU)2,
                                  (HINSTANCE)GetWindowLongPtr(g_hUrlDlg, GWLP_HINSTANCE), nullptr);
        HWND hOk = CreateWindow(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                190, 100, 80, 25, g_hUrlDlg, (HMENU)3,
                                (HINSTANCE)GetWindowLongPtr(g_hUrlDlg, GWLP_HINSTANCE), nullptr);
        ApplyDarkTheme(hEdit);
        ApplyDarkTheme(hCopy);
        ApplyDarkTheme(hOk);
    } else {
        HWND hEdit1 = CreateWindow(L"EDIT", url1.c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
                                   10, 55, 240, 20, g_hUrlDlg, (HMENU)1,
                                   (HINSTANCE)GetWindowLongPtr(g_hUrlDlg, GWLP_HINSTANCE), nullptr);
        HWND hCopy1 = CreateWindow(L"BUTTON", L"Copy Catbox", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   260, 55, 80, 20, g_hUrlDlg, (HMENU)2,
                                   (HINSTANCE)GetWindowLongPtr(g_hUrlDlg, GWLP_HINSTANCE), nullptr);
        HWND hEdit2 = CreateWindow(L"EDIT", url2.c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
                                   10, 85, 240, 20, g_hUrlDlg, (HMENU)4,
                                   (HINSTANCE)GetWindowLongPtr(g_hUrlDlg, GWLP_HINSTANCE), nullptr);
        HWND hCopy2 = CreateWindow(L"BUTTON", L"Copy B2", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   260, 85, 80, 20, g_hUrlDlg, (HMENU)5,
                                   (HINSTANCE)GetWindowLongPtr(g_hUrlDlg, GWLP_HINSTANCE), nullptr);
        HWND hOk = CreateWindow(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                140, 130, 80, 25, g_hUrlDlg, (HMENU)3,
                                (HINSTANCE)GetWindowLongPtr(g_hUrlDlg, GWLP_HINSTANCE), nullptr);
        ApplyDarkTheme(hEdit1);
        ApplyDarkTheme(hCopy1);
        ApplyDarkTheme(hEdit2);
        ApplyDarkTheme(hCopy2);
        ApplyDarkTheme(hOk);
    }
}

LRESULT CALLBACK UrlCopyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == 2 || LOWORD(wParam) == 5) {
            const std::wstring& src = (LOWORD(wParam) == 2) ? g_url1 : g_url2;
            CopyTextToClipboard(hwnd, src);
        } else if (LOWORD(wParam) == 3) {
            DestroyWindow(hwnd);
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;
    case WM_DESTROY:
        g_hUrlDlg = nullptr;
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void GetManualControls(ManualUploadTarget target, HWND& button, HWND& progress, HWND& edit, HWND& copy, HWND& status) {
    if (target == ManualUploadTarget::Catbox) {
        button = g_hCatboxButton;
        progress = g_hCatboxProgress;
        edit = g_hCatboxUrl;
        copy = g_hCatboxCopy;
        status = g_hCatboxStatus;
    } else {
        button = g_hB2Button;
        progress = g_hB2Progress;
        edit = g_hB2Url;
        copy = g_hB2Copy;
        status = g_hB2Status;
    }
}

static void StartManualUpload(ManualUploadTarget target) {
    HWND button = nullptr, progress = nullptr, edit = nullptr, copy = nullptr, status = nullptr;
    GetManualControls(target, button, progress, edit, copy, status);
    if (!progress || !button || g_manualUploadPath.empty() || !g_hManualUploadDlg)
        return;

    if (target == ManualUploadTarget::Catbox) {
        if (g_catboxUploading) return;
        g_catboxUploading = true;
    } else {
        if (g_b2Uploading) return;
        g_b2Uploading = true;
    }

    EnableWindow(button, FALSE);
    if (status) SetWindowTextW(status, L"Uploading...");
    ShowWindow(progress, SW_SHOW);
    SendMessage(progress, PBM_SETPOS, 0, 0);
    if (edit) ShowWindow(edit, SW_HIDE);
    if (copy) ShowWindow(copy, SW_HIDE);
    if (g_hManualClose) EnableWindow(g_hManualClose, FALSE);

    HWND dlg = g_hManualUploadDlg;
    std::wstring path = g_manualUploadPath;
    std::thread([target, dlg, path, progress]() {
        std::string url;
        bool ok = false;
        if (target == ManualUploadTarget::Catbox)
            ok = UploadToCatbox(path, url, progress);
        else
            ok = UploadToB2(path, url, progress);

        auto *result = new ManualUploadResult();
        result->target = target;
        result->success = ok;
        if (ok) {
            int sz = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
            result->url.assign(sz > 0 ? sz - 1 : 0, 0);
            if (sz > 0)
                MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, result->url.data(), sz);
        }
        PostMessage(dlg, WM_MANUAL_UPLOAD_DONE, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

static void HandleManualUploadResult(const ManualUploadResult& result) {
    HWND button = nullptr, progress = nullptr, edit = nullptr, copy = nullptr, status = nullptr;
    GetManualControls(result.target, button, progress, edit, copy, status);

    if (progress) ShowWindow(progress, SW_HIDE);
    if (result.target == ManualUploadTarget::Catbox) {
        g_catboxUploading = false;
        g_catboxUploadSuccess = result.success;
        g_catboxUploadedUrl = result.success ? result.url : L"";
    } else {
        g_b2Uploading = false;
        g_b2UploadSuccess = result.success;
        g_b2UploadedUrl = result.success ? result.url : L"";
    }

    if (!g_catboxUploading && !g_b2Uploading && g_hManualClose)
        EnableWindow(g_hManualClose, TRUE);
    if (button)
        EnableWindow(button, TRUE);

    if (result.success) {
        if (edit) {
            SetWindowTextW(edit, result.url.c_str());
            ShowWindow(edit, SW_SHOW);
        }
        if (copy) ShowWindow(copy, SW_SHOW);
        if (status) SetWindowTextW(status, L"Upload complete.");
    } else {
        if (edit) {
            SetWindowTextW(edit, L"");
            ShowWindow(edit, SW_HIDE);
        }
        if (copy) ShowWindow(copy, SW_HIDE);
        if (status) SetWindowTextW(status, L"Upload failed. Please try again.");
    }
}

void ShowManualUploadDialog(HWND parent, const std::wstring& exportPath, bool allowCatbox, bool allowB2) {
    if (!allowCatbox && !allowB2)
        return;
    if (g_hManualUploadDlg) {
        SetForegroundWindow(g_hManualUploadDlg);
        return;
    }

    g_manualUploadPath = exportPath;
    g_catboxUploading = false;
    g_b2Uploading = false;
    g_catboxUploadSuccess = false;
    g_b2UploadSuccess = false;
    g_catboxUploadedUrl.clear();
    g_b2UploadedUrl.clear();

    int providerCount = (allowCatbox ? 1 : 0) + (allowB2 ? 1 : 0);
    const int width = 600;
    int height = 170 + providerCount * 80;

    g_hManualUploadDlg = CreateWindowEx(0, L"ManualUploadClass", L"Export Complete",
                                        WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
                                        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                                        parent, nullptr,
                                        (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), nullptr);
    if (!g_hManualUploadDlg)
        return;
    ApplyDarkTheme(g_hManualUploadDlg);
    CenterWindow(g_hManualUploadDlg, parent);

    std::wstring successMsg = g_lastOperationWasExport ? L"Video successfully exported." : L"Video successfully cut and saved.";
    CreateWindow(L"STATIC", successMsg.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                 25, 20, width - 50, 20, g_hManualUploadDlg, nullptr,
                 (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);
    CreateWindow(L"STATIC", L"Upload it now? Choose a destination below.", WS_CHILD | WS_VISIBLE | SS_LEFT,
                 25, 45, width - 50, 20, g_hManualUploadDlg, nullptr,
                 (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);

    const int margin = 25;
    const int buttonWidth = 160;
    const int copyWidth = 80;
    const int gap = 15;
    const int progressWidth = width - (margin * 2) - buttonWidth - copyWidth - (gap * 2);

    const int rowHeight = 30;
    const int statusOffset = 34;
    int y = 90;

    if (allowCatbox) {
        g_hCatboxButton = CreateWindow(L"BUTTON", L"Upload to Catbox", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       margin, y, buttonWidth, rowHeight, g_hManualUploadDlg,
                                       (HMENU)(INT_PTR)ID_BTN_CATBOX_UPLOAD,
                                       (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);

        int xComponents = margin + buttonWidth + gap;
        g_hCatboxProgress = CreateWindowEx(0, PROGRESS_CLASS, nullptr, WS_CHILD,
                                           xComponents, y + 3, progressWidth, 24,
                                           g_hManualUploadDlg, nullptr,
                                           (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);
        SendMessage(g_hCatboxProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        ShowWindow(g_hCatboxProgress, SW_HIDE);

        g_hCatboxUrl = CreateWindow(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
                                    xComponents, y + 2, progressWidth, 26,
                                    g_hManualUploadDlg, nullptr,
                                    (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);
        ShowWindow(g_hCatboxUrl, SW_HIDE);

        g_hCatboxCopy = CreateWindow(L"BUTTON", L"Copy", WS_CHILD | BS_PUSHBUTTON,
                                     xComponents + progressWidth + gap, y,
                                     copyWidth, rowHeight, g_hManualUploadDlg,
                                     (HMENU)(INT_PTR)ID_BTN_CATBOX_COPY,
                                     (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);
        ShowWindow(g_hCatboxCopy, SW_HIDE);

        g_hCatboxStatus = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       xComponents, y + statusOffset, progressWidth + copyWidth + gap,
                                       18, g_hManualUploadDlg, nullptr,
                                       (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);

        ApplyDarkTheme(g_hCatboxButton);
        ApplyDarkTheme(g_hCatboxProgress);
        ApplyDarkTheme(g_hCatboxUrl);
        ApplyDarkTheme(g_hCatboxCopy);
        ApplyDarkTheme(g_hCatboxStatus);

        y += 80;
    }

    if (allowB2) {
        g_hB2Button = CreateWindow(L"BUTTON", L"Upload to Backblaze B2", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   margin, y, buttonWidth, rowHeight, g_hManualUploadDlg,
                                   (HMENU)(INT_PTR)ID_BTN_B2_UPLOAD,
                                   (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);

        int xComponents = margin + buttonWidth + gap;
        g_hB2Progress = CreateWindowEx(0, PROGRESS_CLASS, nullptr, WS_CHILD,
                                       xComponents, y + 3, progressWidth, 24,
                                       g_hManualUploadDlg, nullptr,
                                       (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);
        SendMessage(g_hB2Progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        ShowWindow(g_hB2Progress, SW_HIDE);

        g_hB2Url = CreateWindow(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
                                xComponents, y + 2, progressWidth, 26,
                                g_hManualUploadDlg, nullptr,
                                (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);
        ShowWindow(g_hB2Url, SW_HIDE);

        g_hB2Copy = CreateWindow(L"BUTTON", L"Copy", WS_CHILD | BS_PUSHBUTTON,
                                 xComponents + progressWidth + gap, y,
                                 copyWidth, rowHeight, g_hManualUploadDlg,
                                 (HMENU)(INT_PTR)ID_BTN_B2_COPY,
                                 (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);
        ShowWindow(g_hB2Copy, SW_HIDE);

        g_hB2Status = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                   xComponents, y + statusOffset, progressWidth + copyWidth + gap,
                                   18, g_hManualUploadDlg, nullptr,
                                   (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);

        ApplyDarkTheme(g_hB2Button);
        ApplyDarkTheme(g_hB2Progress);
        ApplyDarkTheme(g_hB2Url);
        ApplyDarkTheme(g_hB2Copy);
        ApplyDarkTheme(g_hB2Status);

        if (g_b2KeyId.empty() || g_b2AppKey.empty() || g_b2BucketId.empty() || g_b2BucketName.empty()) {
            EnableWindow(g_hB2Button, FALSE);
            SetWindowTextW(g_hB2Status, L"Backblaze credentials are missing.");
        }

        y += 80;
    }

    int footerY = y;
    g_hOpenFolder = CreateWindow(L"BUTTON", L"Open Containing Folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 margin, footerY, 180, 30, g_hManualUploadDlg,
                                 (HMENU)(INT_PTR)ID_BTN_OPEN_FOLDER,
                                 (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);
    g_hManualClose = CreateWindow(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  width - margin - 80, footerY, 80, 30, g_hManualUploadDlg,
                                  (HMENU)(INT_PTR)ID_BTN_MANUAL_CLOSE,
                                  (HINSTANCE)GetWindowLongPtr(g_hManualUploadDlg, GWLP_HINSTANCE), nullptr);
    ApplyDarkTheme(g_hManualClose);
    ApplyDarkTheme(g_hOpenFolder);
}

LRESULT CALLBACK ManualUploadProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(BLACK_BRUSH);
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_CATBOX_UPLOAD:
            StartManualUpload(ManualUploadTarget::Catbox);
            break;
        case ID_BTN_B2_UPLOAD:
            StartManualUpload(ManualUploadTarget::B2);
            break;
        case ID_BTN_CATBOX_COPY:
            CopyTextToClipboard(hwnd, g_catboxUploadedUrl);
            break;
        case ID_BTN_B2_COPY:
            CopyTextToClipboard(hwnd, g_b2UploadedUrl);
            break;
        case ID_BTN_OPEN_FOLDER:
            if (!g_manualUploadPath.empty()) {
                std::wstring arg = L"/select,\"" + g_manualUploadPath + L"\"";
                ShellExecuteW(hwnd, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
            }
            break;
        case ID_BTN_MANUAL_CLOSE:
            if (g_catboxUploading || g_b2Uploading)
                return 0;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_MANUAL_UPLOAD_DONE:
        {
            auto *result = reinterpret_cast<ManualUploadResult*>(lParam);
            if (result) {
                HandleManualUploadResult(*result);
                delete result;
            }
        }
        return 0;
    case WM_CLOSE:
        if (g_catboxUploading || g_b2Uploading)
            return 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_hManualUploadDlg = nullptr;
        g_manualUploadPath.clear();
        g_hCatboxButton = g_hCatboxProgress = g_hCatboxUrl = g_hCatboxCopy = g_hCatboxStatus = nullptr;
        g_hB2Button = g_hB2Progress = g_hB2Url = g_hB2Copy = g_hB2Status = nullptr;
        g_hManualClose = nullptr;
        g_hOpenFolder = nullptr;
        g_catboxUploading = false;
        g_b2Uploading = false;
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
