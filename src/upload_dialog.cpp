#include "upload_dialog.h"
#include "utils.h"
#include "catbox_upload.h"
#include "b2_upload.h"
#include "editing.h"
#include "options_window.h"
#include <commctrl.h>
#include <shellapi.h>
#include <thread>
#include <uxtheme.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

// --- Colours ---
static constexpr COLORREF CLR_BG      = RGB(30,  30,  32);
static constexpr COLORREF CLR_CARD    = RGB(42,  42,  46);
static constexpr COLORREF CLR_BORDER  = RGB(65,  65,  70);
static constexpr COLORREF CLR_ACCENT  = RGB(0,  120, 212);
static constexpr COLORREF CLR_SUCCESS = RGB(22, 198,  12);
static constexpr COLORREF CLR_ERROR   = RGB(232,  17,  35);
static constexpr COLORREF CLR_TEXT    = RGB(240, 240, 242);
static constexpr COLORREF CLR_MUTED   = RGB(160, 160, 165);
static constexpr COLORREF CLR_FOOTER  = RGB(24,  24,  26);
static constexpr COLORREF CLR_HEADER  = RGB(36,  36,  40);

// --- Layout ---
static constexpr int M   = 18;   // outer margin
static constexpr int P   =  6;   // inner padding
static constexpr int BW  = 160;  // upload button width
static constexpr int CW  =  68;  // copy button width
static constexpr int RH  =  27;  // row height
static constexpr int PBH =  12;  // progress bar height
static constexpr int EH  =  22;  // edit height
static constexpr int SH  =  14;  // status label height
static constexpr int HDR =  58;  // header band height
static constexpr int FTR =  40;  // footer band height
static constexpr int CRH =  64;  // card row height (per provider)

// --- Globals: URL copy dialog ---
static HWND         g_hUrlDlg = nullptr;
static std::wstring g_url1;
static std::wstring g_url2;

// --- Globals: manual upload dialog ---
static HWND         g_hManualUploadDlg = nullptr;
static std::wstring g_manualUploadPath;

static HWND g_hCatboxButton   = nullptr;
static HWND g_hCatboxProgress = nullptr;
static HWND g_hCatboxUrl      = nullptr;
static HWND g_hCatboxCopy     = nullptr;
static HWND g_hCatboxStatus   = nullptr;

static HWND g_hB2Button       = nullptr;
static HWND g_hB2Progress     = nullptr;
static HWND g_hB2Url          = nullptr;
static HWND g_hB2Copy         = nullptr;
static HWND g_hB2Status       = nullptr;

static HWND g_hManualClose    = nullptr;
static HWND g_hOpenFolder     = nullptr;
static HWND g_hOpenVideo      = nullptr;

static bool g_catboxUploading = false;
static bool g_b2Uploading     = false;

enum class ManualUploadTarget { Catbox = 1, B2 = 2 };

struct ManualUploadResult {
    ManualUploadTarget target;
    bool               success;
    std::wstring       url;
};

constexpr UINT WM_MANUAL_UPLOAD_DONE = WM_APP + 2;
constexpr int  ID_BTN_CATBOX_UPLOAD  = 4101;
constexpr int  ID_BTN_B2_UPLOAD      = 4102;
constexpr int  ID_BTN_CATBOX_COPY    = 4103;
constexpr int  ID_BTN_B2_COPY        = 4104;
constexpr int  ID_BTN_MANUAL_CLOSE   = 4105;
constexpr int  ID_BTN_OPEN_FOLDER    = 4106;
constexpr int  ID_BTN_OPEN_VIDEO     = 4107;

// ─── Font helpers ─────────────────────────────────────────────────────────────
static HFONT GetBoldFont()
{
    static HFONT f = nullptr;
    if (!f) f = CreateFont(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    return f;
}
static HFONT GetSmallFont()
{
    static HFONT f = nullptr;
    if (!f) f = CreateFont(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    return f;
}

// ─── Clipboard ────────────────────────────────────────────────────────────────
static void CopyTextToClipboard(HWND hwnd, const std::wstring& text)
{
    if (text.empty()) return;
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t  sz   = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (hMem) {
        memcpy(GlobalLock(hMem), text.c_str(), sz);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
}

// ─── Control factory helpers ──────────────────────────────────────────────────
static HINSTANCE HInst(HWND hwnd) {
    return (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
}

static HWND Btn(HWND parent, const wchar_t* t, int x, int y, int w, int h,
                int id, DWORD extra = 0)
{
    HWND hb = CreateWindowEx(0, L"BUTTON", t,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | extra,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id, HInst(parent), nullptr);
    ApplyDarkTheme(hb);
    return hb;
}

static HWND Lbl(HWND parent, const wchar_t* t, int x, int y, int w, int h,
                HFONT font = nullptr, COLORREF col = 0)
{
    HWND hs = CreateWindowEx(0, L"STATIC", t,
                             WS_CHILD | WS_VISIBLE | SS_LEFT,
                             x, y, w, h, parent, nullptr, HInst(parent), nullptr);
    SendMessage(hs, WM_SETFONT, (WPARAM)(font ? font : GetBoldFont()), TRUE);
    (void)col;
    return hs;
}

static HWND Edit(HWND parent, int x, int y, int w, int h)
{
    HWND he = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | ES_AUTOHSCROLL | ES_READONLY,
                             x, y, w, h, parent, nullptr, HInst(parent), nullptr);
    ApplyDarkTheme(he);
    return he;
}

static HWND ProgBar(HWND parent, int x, int y, int w, int h)
{
    HWND hpb = CreateWindowEx(0, PROGRESS_CLASS, nullptr,
                              WS_CHILD | PBS_SMOOTH,
                              x, y, w, h, parent, nullptr, HInst(parent), nullptr);
    SendMessage(hpb, PBM_SETRANGE,    0, MAKELPARAM(0, 100));
    SendMessage(hpb, PBM_SETBARCOLOR, 0, (LPARAM)CLR_ACCENT);
    SendMessage(hpb, PBM_SETBKCOLOR,  0, (LPARAM)CLR_CARD);
    ApplyDarkTheme(hpb);
    return hpb;
}

static void HSep(HWND parent, int x, int y, int w)
{
    HWND hs = CreateWindowEx(0, L"STATIC", nullptr,
                             WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                             x, y, w, 2, parent, nullptr, HInst(parent), nullptr);
    SetWindowTheme(hs, L"", L"");
}

// ─────────────────────────────────────────────────────────────────────────────
// URL Copy Dialog
// ─────────────────────────────────────────────────────────────────────────────
void ShowUrlCopyDialog(HWND parent, const std::wstring& message,
                       const std::wstring& url1, const std::wstring& url2)
{
    g_url1 = url1;
    g_url2 = url2;
    if (g_hUrlDlg) DestroyWindow(g_hUrlDlg);

    bool two = !url2.empty();
    int  W = 440, H = two ? 260 : 210;

    g_hUrlDlg = CreateWindowEx(0, L"UrlCopyClass", L"Upload Complete",
                               WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
                               CW_USEDEFAULT, CW_USEDEFAULT, W, H,
                               parent, nullptr, HInst(parent), nullptr);
    if (!g_hUrlDlg) return;
    ApplyDarkTheme(g_hUrlDlg);
    CenterWindow(g_hUrlDlg, parent);

    int cx = W - 2 * M;

    // Header
    Lbl(g_hUrlDlg, L"Upload Complete", M, 14, cx, 20, GetBoldFont());
    Lbl(g_hUrlDlg, message.c_str(),    M, 36, cx, 16, GetSmallFont());
    HSep(g_hUrlDlg, M, 58, cx);

    if (!two) {
        Lbl(g_hUrlDlg, L"URL", M, 68, cx, 14, GetSmallFont());
        HWND hE = Edit(g_hUrlDlg, M, 85, cx, EH);
        SetWindowTextW(hE, url1.c_str());
        Btn(g_hUrlDlg, L"Copy URL", M,           138, 100, RH, 2);
        Btn(g_hUrlDlg, L"OK",       W - M - 80,  138,  80, RH, 3, BS_DEFPUSHBUTTON);
    } else {
        Lbl(g_hUrlDlg, L"Catbox",     M, 68, cx, 14, GetSmallFont());
        HWND hE1 = Edit(g_hUrlDlg, M, 84, cx - CW - P, EH);
        SetWindowTextW(hE1, url1.c_str());
        Btn(g_hUrlDlg, L"Copy", W - M - CW, 84, CW, EH, 2);

        Lbl(g_hUrlDlg, L"Backblaze B2", M, 116, cx, 14, GetSmallFont());
        HWND hE2 = Edit(g_hUrlDlg, M, 132, cx - CW - P, EH);
        SetWindowTextW(hE2, url2.c_str());
        Btn(g_hUrlDlg, L"Copy", W - M - CW, 132, CW, EH, 5);

        HSep(g_hUrlDlg, M, 168, cx);
        Btn(g_hUrlDlg, L"OK", W - M - 80, 178, 80, RH, 3, BS_DEFPUSHBUTTON);
    }
}

LRESULT CALLBACK UrlCopyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == 2 || LOWORD(wParam) == 5)
            CopyTextToClipboard(hwnd, LOWORD(wParam) == 2 ? g_url1 : g_url2);
        else if (LOWORD(wParam) == 3)
            DestroyWindow(hwnd);
        break;
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, CreateSolidBrush(CLR_BG));
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        static HBRUSH hbr = CreateSolidBrush(CLR_BG);
        SetTextColor((HDC)wParam, CLR_TEXT);
        SetBkColor((HDC)wParam, CLR_BG);
        return (LRESULT)hbr;
    }
    case WM_CTLCOLORBTN: {
        static HBRUSH hbr = CreateSolidBrush(CLR_BG);
        SetBkColor((HDC)wParam, CLR_BG);
        return (LRESULT)hbr;
    }
    case WM_CLOSE:    DestroyWindow(hwnd); break;
    case WM_DESTROY:  g_hUrlDlg = nullptr; break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// Manual upload helpers
// ─────────────────────────────────────────────────────────────────────────────
static void GetManualControls(ManualUploadTarget t,
                               HWND& btn, HWND& pb, HWND& ed, HWND& cp, HWND& st)
{
    if (t == ManualUploadTarget::Catbox) {
        btn = g_hCatboxButton; pb = g_hCatboxProgress;
        ed  = g_hCatboxUrl;    cp = g_hCatboxCopy;  st = g_hCatboxStatus;
    } else {
        btn = g_hB2Button; pb = g_hB2Progress;
        ed  = g_hB2Url;    cp = g_hB2Copy;  st = g_hB2Status;
    }
}

static void StartManualUpload(ManualUploadTarget target)
{
    HWND btn = nullptr, pb = nullptr, ed = nullptr, cp = nullptr, st = nullptr;
    GetManualControls(target, btn, pb, ed, cp, st);
    if (!pb || !btn || g_manualUploadPath.empty() || !g_hManualUploadDlg) return;

    if (target == ManualUploadTarget::Catbox) { if (g_catboxUploading) return; g_catboxUploading = true; }
    else                                       { if (g_b2Uploading)     return; g_b2Uploading     = true; }

    EnableWindow(btn, FALSE);
    if (st) SetWindowTextW(st, L"Uploading...");
    SendMessage(pb, PBM_SETBARCOLOR, 0, (LPARAM)CLR_ACCENT);
    SendMessage(pb, PBM_SETPOS, 0, 0);
    ShowWindow(pb, SW_SHOW);
    if (ed) ShowWindow(ed, SW_HIDE);
    if (cp) ShowWindow(cp, SW_HIDE);
    if (g_hManualClose) EnableWindow(g_hManualClose, FALSE);

    HWND        dlg  = g_hManualUploadDlg;
    std::wstring path = g_manualUploadPath;

    std::thread([target, dlg, path, pb]() {
        std::string url;
        bool ok = (target == ManualUploadTarget::Catbox)
                  ? UploadToCatbox(path, url, pb)
                  : UploadToB2(path, url, pb);
        auto* res    = new ManualUploadResult();
        res->target  = target;
        res->success = ok;
        if (ok) {
            int sz = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
            res->url.assign(sz > 0 ? sz - 1 : 0, 0);
            if (sz > 0) MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, res->url.data(), sz);
        }
        PostMessage(dlg, WM_MANUAL_UPLOAD_DONE, 0, reinterpret_cast<LPARAM>(res));
    }).detach();
}

static void HandleManualUploadResult(const ManualUploadResult& r)
{
    HWND btn = nullptr, pb = nullptr, ed = nullptr, cp = nullptr, st = nullptr;
    GetManualControls(r.target, btn, pb, ed, cp, st);

    if (r.target == ManualUploadTarget::Catbox) {
        g_catboxUploading    = false;
        g_catboxUploadSuccess = r.success;
        g_catboxUploadedUrl  = r.success ? r.url : L"";
    } else {
        g_b2Uploading    = false;
        g_b2UploadSuccess = r.success;
        g_b2UploadedUrl  = r.success ? r.url : L"";
    }

    if (!g_catboxUploading && !g_b2Uploading && g_hManualClose)
        EnableWindow(g_hManualClose, TRUE);
    if (btn) EnableWindow(btn, TRUE);

    if (r.success) {
        if (pb) { SendMessage(pb, PBM_SETBARCOLOR, 0, (LPARAM)CLR_SUCCESS); ShowWindow(pb, SW_SHOW); }
        if (ed) { SetWindowTextW(ed, r.url.c_str()); ShowWindow(ed, SW_SHOW); }
        if (cp) ShowWindow(cp, SW_SHOW);
        if (st) SetWindowTextW(st, L"Upload complete.");
    } else {
        if (pb) ShowWindow(pb, SW_HIDE);
        if (ed) { SetWindowTextW(ed, L""); ShowWindow(ed, SW_HIDE); }
        if (cp) ShowWindow(cp, SW_HIDE);
        if (st) SetWindowTextW(st, L"Upload failed. Please try again.");
    }
    if (g_hManualUploadDlg) InvalidateRect(g_hManualUploadDlg, nullptr, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowManualUploadDialog
// ─────────────────────────────────────────────────────────────────────────────
void ShowManualUploadDialog(HWND parent, const std::wstring& exportPath,
                             bool allowCatbox, bool allowB2)
{
    if (!allowCatbox && !allowB2) return;
    if (g_hManualUploadDlg) { SetForegroundWindow(g_hManualUploadDlg); return; }

    g_manualUploadPath    = exportPath;
    g_catboxUploading     = false;
    g_b2Uploading         = false;
    g_catboxUploadSuccess = false;
    g_b2UploadSuccess     = false;
    g_catboxUploadedUrl.clear();
    g_b2UploadedUrl.clear();

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    int providers = (allowCatbox ? 1 : 0) + (allowB2 ? 1 : 0);
    const int W_client = 520;
    const int H_client = HDR + P + (providers * CRH) + P + FTR;

    RECT rc = { 0, 0, W_client, H_client };
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_POPUPWINDOW, FALSE, 0);
    int W = rc.right - rc.left;
    int H = rc.bottom - rc.top;

    g_hManualUploadDlg = CreateWindowEx(0, L"ManualUploadClass", L"Export Complete",
                                        WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
                                        CW_USEDEFAULT, CW_USEDEFAULT, W, H,
                                        parent, nullptr, HInst(parent), nullptr);
    if (!g_hManualUploadDlg) return;
    ApplyDarkTheme(g_hManualUploadDlg);
    CenterWindow(g_hManualUploadDlg, parent);

    int cx = W_client - 2 * M;
    // --- Header ---
    std::wstring title = g_lastOperationWasExport
                         ? L"Video exported successfully."
                         : L"Video cut and saved successfully.";
    Lbl(g_hManualUploadDlg, title.c_str(), M, 11, cx, 20, GetBoldFont());
    Lbl(g_hManualUploadDlg, L"Select a destination to upload the file:",
        M, 32, cx, 15, GetSmallFont());
    HSep(g_hManualUploadDlg, M, HDR - 2, cx);

    // --- Provider rows ---
    int y    = HDR + P;
    int pbX  = M + BW + P;
    int pbW  = cx - BW - P - CW - P;

    auto MakeRow = [&](bool allow,
                       const wchar_t* sectionLabel,
                       const wchar_t* btnLabel,
                       int            btnId,
                       int            copyId,
                       HWND&          outBtn,
                       HWND&          outPb,
                       HWND&          outEd,
                       HWND&          outCp,
                       HWND&          outSt,
                       bool           disabled,
                       const wchar_t* disabledMsg)
    {
        if (!allow) return;

        // Section label (small, muted)
        Lbl(g_hManualUploadDlg, sectionLabel, M, y, cx, 13, GetSmallFont());

        int rowY = y + 14;

        // Upload button
        outBtn = Btn(g_hManualUploadDlg, btnLabel, M, rowY, BW, RH, btnId);

        // Progress bar (centred vertically within row)
        int pbY = rowY + (RH - PBH) / 2;
        outPb = ProgBar(g_hManualUploadDlg, pbX, pbY, pbW, PBH);
        ShowWindow(outPb, SW_HIDE);

        // URL edit (same vertical zone as progress bar)
        int edY = rowY + (RH - EH) / 2;
        outEd = Edit(g_hManualUploadDlg, pbX, edY, pbW, EH);
        ShowWindow(outEd, SW_HIDE);

        // Copy button
        outCp = Btn(g_hManualUploadDlg, L"Copy",
                    M + BW + P + pbW + P, rowY, CW, RH, copyId);
        ShowWindow(outCp, SW_HIDE);

        // Status label (sits just below the button row)
        outSt = Lbl(g_hManualUploadDlg, L"",
                    pbX, rowY + RH + 2, pbW + CW + P, SH, GetSmallFont());

        if (disabled) {
            EnableWindow(outBtn, FALSE);
            SetWindowTextW(outSt, disabledMsg);
        }

        y += CRH;
    };

    MakeRow(allowCatbox,
            L"Catbox",
            L"Upload to Catbox",
            ID_BTN_CATBOX_UPLOAD, ID_BTN_CATBOX_COPY,
            g_hCatboxButton, g_hCatboxProgress,
            g_hCatboxUrl, g_hCatboxCopy, g_hCatboxStatus,
            false, nullptr);

    bool b2Bad = g_b2KeyId.empty() || g_b2AppKey.empty()
              || g_b2BucketId.empty() || g_b2BucketName.empty();
    MakeRow(allowB2,
            L"Backblaze B2",
            L"Upload to Backblaze B2",
            ID_BTN_B2_UPLOAD, ID_BTN_B2_COPY,
            g_hB2Button, g_hB2Progress,
            g_hB2Url, g_hB2Copy, g_hB2Status,
            b2Bad, L"Credentials not configured. Open Options > Upload.");

    // --- Footer: Open Containing Folder | Open Video  ... Close ---
    int sepY = H_client - FTR - 1;
    int btnY = sepY + (FTR - RH) / 2;
    HSep(g_hManualUploadDlg, M, sepY, cx);
    g_hOpenFolder = Btn(g_hManualUploadDlg, L"Open Containing Folder",
                        M, btnY, 162, RH, ID_BTN_OPEN_FOLDER);
    g_hOpenVideo  = Btn(g_hManualUploadDlg, L"Open Video",
                        M + 162 + 6, btnY, 100, RH, ID_BTN_OPEN_VIDEO);
    g_hManualClose = Btn(g_hManualUploadDlg, L"Close",
                         W_client - M - 80, btnY, 80, RH, ID_BTN_MANUAL_CLOSE,
                         BS_DEFPUSHBUTTON);
}

// ─────────────────────────────────────────────────────────────────────────────
// ManualUploadProc
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK ManualUploadProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_ERASEBKGND: {
        HDC  hdc = (HDC)wParam;
        RECT rc;  GetClientRect(hwnd, &rc);
        // Base background
        HBRUSH hbr = CreateSolidBrush(CLR_BG);
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);
        // Header band
        RECT rh = { 0, 0, rc.right, HDR };
        hbr = CreateSolidBrush(CLR_HEADER);
        FillRect(hdc, &rh, hbr);
        DeleteObject(hbr);
        // Footer band
        RECT rf = { 0, rc.bottom - FTR, rc.right, rc.bottom };
        hbr = CreateSolidBrush(CLR_FOOTER);
        FillRect(hdc, &rf, hbr);
        DeleteObject(hbr);
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC  hdc = (HDC)wParam;
        HWND hCtl = (HWND)lParam;
        RECT wr;  GetWindowRect(hCtl, &wr);
        POINT pt = { wr.left, wr.top };
        ScreenToClient(hwnd, &pt);
        RECT cr;  GetClientRect(hwnd, &cr);

        COLORREF bg = CLR_BG;
        if (pt.y < HDR)                     bg = CLR_HEADER;
        else if (pt.y > cr.bottom - FTR)    bg = CLR_FOOTER;

        // Status text colour
        wchar_t buf[256] = {};
        GetWindowTextW(hCtl, buf, _countof(buf));
        if (wcsstr(buf, L"complete"))          SetTextColor(hdc, CLR_SUCCESS);
        else if (wcsstr(buf, L"failed") ||
                 wcsstr(buf, L"not configured")) SetTextColor(hdc, CLR_ERROR);
        else if (wcsstr(buf, L"Uploading"))    SetTextColor(hdc, CLR_ACCENT);
        else if (bg == CLR_HEADER)             SetTextColor(hdc, CLR_TEXT);
        else                                   SetTextColor(hdc, CLR_MUTED);

        SetBkColor(hdc, bg);
        static HBRUSH hbrBg  = CreateSolidBrush(CLR_BG);
        static HBRUSH hbrHdr = CreateSolidBrush(CLR_HEADER);
        static HBRUSH hbrFtr = CreateSolidBrush(CLR_FOOTER);
        return (LRESULT)(bg == CLR_HEADER ? hbrHdr : bg == CLR_FOOTER ? hbrFtr : hbrBg);
    }

    case WM_CTLCOLORBTN: {
        static HBRUSH hbr = CreateSolidBrush(CLR_BG);
        SetBkColor((HDC)wParam, CLR_BG);
        return (LRESULT)hbr;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_CATBOX_UPLOAD: StartManualUpload(ManualUploadTarget::Catbox); break;
        case ID_BTN_B2_UPLOAD:     StartManualUpload(ManualUploadTarget::B2);     break;
        case ID_BTN_CATBOX_COPY:   CopyTextToClipboard(hwnd, g_catboxUploadedUrl); break;
        case ID_BTN_B2_COPY:       CopyTextToClipboard(hwnd, g_b2UploadedUrl);    break;
        case ID_BTN_OPEN_FOLDER:
            if (!g_manualUploadPath.empty()) {
                std::wstring arg = L"/select,\"" + g_manualUploadPath + L"\"";
                ShellExecuteW(hwnd, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
            }
            break;
        case ID_BTN_OPEN_VIDEO:
            if (!g_manualUploadPath.empty())
                ShellExecuteW(hwnd, L"open", g_manualUploadPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case ID_BTN_MANUAL_CLOSE:
            if (g_catboxUploading || g_b2Uploading) return 0;
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_MANUAL_UPLOAD_DONE: {
        auto* r = reinterpret_cast<ManualUploadResult*>(lParam);
        if (r) { HandleManualUploadResult(*r); delete r; }
        return 0;
    }

    case WM_CLOSE:
        if (g_catboxUploading || g_b2Uploading) return 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_hManualUploadDlg = nullptr;
        g_manualUploadPath.clear();
        g_hCatboxButton = g_hCatboxProgress = g_hCatboxUrl =
        g_hCatboxCopy   = g_hCatboxStatus   = nullptr;
        g_hB2Button     = g_hB2Progress     = g_hB2Url =
        g_hB2Copy       = g_hB2Status       = nullptr;
        g_hManualClose  = nullptr;
        g_hOpenFolder   = nullptr;
        g_hOpenVideo    = nullptr;
        g_catboxUploading = g_b2Uploading = false;
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
