#include "upload_dialog.h"
#include "utils.h"

static HWND g_hUrlDlg = nullptr;
static std::wstring g_url1;
static std::wstring g_url2;

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
            if (OpenClipboard(hwnd)) {
                EmptyClipboard();
                size_t sz = (src.size() + 1) * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                if (hMem) {
                    memcpy(GlobalLock(hMem), src.c_str(), sz);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                CloseClipboard();
            }
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

