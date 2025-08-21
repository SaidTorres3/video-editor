#include "window_proc.h"
#include "video_player.h"
#include "options_window.h"
#include "progress_window.h"
#include "ui_controls.h"
#include "file_handling.h"
#include "ui_updates.h"
#include "editing.h"
#include "upload_dialog.h"
#include "utils.h"
#include <windowsx.h>

// Forward declarations for functions in other files
void ApplyDarkTheme(HWND hwnd);
void RepositionControls(HWND hwnd);
void OpenVideoFile(HWND hwnd);
void LoadVideoFile(HWND hwnd, const std::wstring& filename);
void UpdateControls();
void UpdateTimeline();
void OnMuteTrackClicked();
void OnTrackVolumeChanged();
void OnMasterVolumeChanged();
void OnSetStartClicked(HWND hwnd);
void OnSetEndClicked(HWND hwnd);
void OnCutClicked(HWND hwnd);
void OnExportClicked(HWND hwnd);
void OnAudioTrackSelectionChanged();
double ParseTimeString(const std::wstring& str);
void UpdateCutInfoLabel(HWND hwnd);
void UpdateCutTimeEdits();


extern VideoPlayer *g_videoPlayer;
extern HWND g_hEditStartTime, g_hEditEndTime, g_hListBoxAudioTracks, g_hSliderTrackVolume, g_hSliderMasterVolume, g_hRadioH264, g_hEditBitrate, g_hEditTargetSize, g_hRadioUseBitrate, g_hRadioUseSize, g_hLabelBitrate, g_hLabelTargetSize;
extern double g_cutStartTime;
extern double g_cutEndTime;
extern bool g_lastOperationWasExport;
extern bool g_uploadSuccess;
extern std::wstring g_catboxUploadedUrl;
extern std::wstring g_b2UploadedUrl;
extern bool g_catboxUploadSuccess;
extern bool g_b2UploadSuccess;
extern bool g_autoUpload;
extern HBRUSH g_hbrBackground;
extern HFONT g_hFont;
extern COLORREF g_textColor;

// Context menu IDs for noise reduction levels
#define ID_NR_DISABLED 2001
#define ID_NR_LOW      2002
#define ID_NR_NORMAL   2003
#define ID_NR_HIGH     2004


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        // Initialize dark theme resources
        g_hbrBackground = CreateSolidBrush(RGB(45, 45, 48));
        g_hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        CreateControls(hwnd);
        g_videoPlayer = new VideoPlayer(hwnd);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)g_videoPlayer);
        SetTimer(hwnd, 1006, 100, nullptr); // ID_TIMER_UPDATE
        DragAcceptFiles(hwnd, TRUE);

        // Apply dark theme to window itself
        ApplyDarkTheme(hwnd);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case 1001: // ID_BUTTON_OPEN
            OpenVideoFile(hwnd);
            break;
        case 1002: // ID_BUTTON_PLAY
            if (g_videoPlayer && g_videoPlayer->IsLoaded())
            {
                g_videoPlayer->Play();
                UpdateControls();
            }
            break;
        case 1003: // ID_BUTTON_PAUSE
            if (g_videoPlayer)
            {
                g_videoPlayer->Pause();
                UpdateControls();
            }
            break;
        case 1004: // ID_BUTTON_STOP
            if (g_videoPlayer)
            {
                g_videoPlayer->Stop();
                UpdateControls();
                UpdateTimeline();
            }
            break;
        case 1020: // ID_BUTTON_OPTIONS
            ShowOptionsWindow(hwnd);
            break;
        case 1008: // ID_BUTTON_MUTE_TRACK
            OnMuteTrackClicked();
            break;
        case 1011: // ID_BUTTON_SET_START
            OnSetStartClicked(hwnd);
            break;
        case 1012: // ID_BUTTON_SET_END
            OnSetEndClicked(hwnd);
            break;
        case 1013: // ID_BUTTON_CUT
            if (g_cutStartTime < 0 && g_cutEndTime < 0)
                OnExportClicked(hwnd);
            else
                OnCutClicked(hwnd);
            break;
        case 1015: // ID_RADIO_COPY_CODEC
        case 1016: // ID_RADIO_H264
        case 1024: // ID_RADIO_USE_BITRATE
        case 1025: // ID_RADIO_USE_SIZE
            UpdateControls();
            break;
        case ID_NR_DISABLED:
        case ID_NR_LOW:
        case ID_NR_NORMAL:
        case ID_NR_HIGH:
        {
            int sel = (int)SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && g_videoPlayer)
            {
                NoiseReductionLevel level = NoiseReductionLevel::Disabled;
                if (LOWORD(wParam) == ID_NR_LOW) level = NoiseReductionLevel::Low;
                else if (LOWORD(wParam) == ID_NR_NORMAL) level = NoiseReductionLevel::Normal;
                else if (LOWORD(wParam) == ID_NR_HIGH) level = NoiseReductionLevel::High;
                g_videoPlayer->SetNoiseReductionLevel(sel, level);
            }
            break;
        }
        }

        if ((HWND)lParam == g_hEditStartTime && HIWORD(wParam) == EN_KILLFOCUS)
        {
            wchar_t buf[64];
            GetWindowTextW(g_hEditStartTime, buf, 64);
            if (wcslen(buf) == 0)
            {
                g_cutStartTime = -1.0;
            }
            else
            {
                double t = ParseTimeString(buf);
                if (t >= 0)
                {
                    g_cutStartTime = t;
                    if (g_cutEndTime >= 0 && g_cutStartTime >= g_cutEndTime)
                        g_cutEndTime = -1.0;
                }
                else
                {
                    g_cutStartTime = -1.0;
                }
            }
            UpdateCutInfoLabel(hwnd);
            UpdateCutTimeEdits();
            UpdateTimeline();
        }
        else if ((HWND)lParam == g_hEditEndTime && HIWORD(wParam) == EN_KILLFOCUS)
        {
            wchar_t buf[64];
            GetWindowTextW(g_hEditEndTime, buf, 64);
            if (wcslen(buf) == 0)
            {
                g_cutEndTime = -1.0;
            }
            else
            {
                double t = ParseTimeString(buf);
                if (t >= 0)
                {
                    g_cutEndTime = t;
                    if (g_cutStartTime >= 0 && g_cutEndTime <= g_cutStartTime)
                        g_cutEndTime = -1.0;
                }
                else
                {
                    g_cutEndTime = -1.0;
                }
            }
            UpdateCutInfoLabel(hwnd);
            UpdateCutTimeEdits();
            UpdateTimeline();
        }

        // Handle listbox selection changes
        if (HIWORD(wParam) == LBN_SELCHANGE && (HWND)lParam == g_hListBoxAudioTracks)
        {
            OnAudioTrackSelectionChanged();
        }
        break;

    case WM_CONTEXTMENU:
        if ((HWND)wParam == g_hListBoxAudioTracks && g_videoPlayer)
        {
            int sel = (int)SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR)
            {
                HMENU menu = CreatePopupMenu();
                NoiseReductionLevel level = g_videoPlayer->GetNoiseReductionLevel(sel);
                AppendMenu(menu, MF_STRING | (level == NoiseReductionLevel::Disabled ? MF_CHECKED : 0), ID_NR_DISABLED, L"Disabled");
                AppendMenu(menu, MF_STRING | (level == NoiseReductionLevel::Low ? MF_CHECKED : 0), ID_NR_LOW, L"Low");
                AppendMenu(menu, MF_STRING | (level == NoiseReductionLevel::Normal ? MF_CHECKED : 0), ID_NR_NORMAL, L"Normal");
                AppendMenu(menu, MF_STRING | (level == NoiseReductionLevel::High ? MF_CHECKED : 0), ID_NR_HIGH, L"High");
                TrackPopupMenu(menu, TPM_RIGHTBUTTON, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0, hwnd, NULL);
                DestroyMenu(menu);
            }
            return 0;
        }
        break;

    case WM_HSCROLL:
        if ((HWND)lParam == g_hSliderTrackVolume)
        {
            OnTrackVolumeChanged();
        }
        else if ((HWND)lParam == g_hSliderMasterVolume)
        {
            OnMasterVolumeChanged();
        }
        break;

    case WM_MOUSEWHEEL:
    {
        HWND focused = GetFocus();
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        double step = 0.1 * (delta / WHEEL_DELTA);
        if (focused == g_hEditStartTime && g_cutStartTime >= 0)
        {
            g_cutStartTime += step;
            if (g_cutStartTime < 0) g_cutStartTime = 0;
            if (g_cutEndTime >= 0 && g_cutStartTime >= g_cutEndTime)
                g_cutStartTime = g_cutEndTime - 0.01;
            UpdateCutInfoLabel(hwnd);
            UpdateCutTimeEdits();
            UpdateTimeline();
        }
        else if (focused == g_hEditEndTime && g_cutEndTime >= 0)
        {
            g_cutEndTime += step;
            if (g_cutEndTime < 0) g_cutEndTime = 0;
            if (g_cutStartTime >= 0 && g_cutEndTime <= g_cutStartTime)
                g_cutEndTime = g_cutStartTime + 0.01;
            UpdateCutInfoLabel(hwnd);
            UpdateCutTimeEdits();
            UpdateTimeline();
        }
    }
    break;

    case WM_TIMER:
        if (wParam == 1006) // ID_TIMER_UPDATE
        {
            UpdateTimeline();
            UpdateControls();
        }
        break;

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;
        wchar_t filePath[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH))
        {
            LoadVideoFile(hwnd, std::wstring(filePath));
        }
        DragFinish(hDrop);
    }
    break;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_textColor);
        SetBkColor(hdc, RGB(45,45,48));
        return (INT_PTR)g_hbrBackground;
    }
    break;

    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, g_hbrBackground);
        return 1;
    }
    break;

    case WM_SIZE:
    {
        RepositionControls(hwnd);
    }
    break;

        case (WM_APP + 1): // WM_APP_CUT_DONE
        {
            CloseProgressWindow();
            EnableWindow(hwnd, TRUE);
            bool success = wParam != 0;
            if (success && g_autoUpload && g_uploadSuccess) {
                std::wstring m = g_lastOperationWasExport ? L"Video successfully exported." : L"Video successfully cut and saved.";
                if (g_useCatbox && g_useB2)
                    m += L"\nUploaded to catbox.moe and Backblaze B2:";
                else if (g_useCatbox)
                    m += L"\nUploaded to catbox.moe:";
                else if (g_useB2)
                    m += L"\nUploaded to Backblaze B2:";
                ShowUrlCopyDialog(hwnd, m, g_catboxUploadedUrl, g_b2UploadedUrl);
            } else {
                const wchar_t* msg;
                const wchar_t* title;
                UINT flags;
                if (success) {
                    if (g_autoUpload) {
                        if (g_useCatbox || g_useB2) {
                            std::wstring m = g_lastOperationWasExport ? L"Video successfully exported." : L"Video successfully cut and saved.";
                            if (g_useCatbox) {
                                if (g_catboxUploadSuccess)
                                    m += L"\nUploaded to catbox.moe:\n" + g_catboxUploadedUrl;
                                else
                                    m += L"\nFailed to upload to catbox.moe.";
                            }
                            if (g_useB2) {
                                if (g_b2UploadSuccess)
                                    m += L"\nUploaded to Backblaze B2:\n" + g_b2UploadedUrl;
                                else
                                    m += L"\nFailed to upload to Backblaze B2.";
                            }
                            msg = m.c_str();
                        } else {
                            msg = g_lastOperationWasExport ? L"Video successfully exported." : L"Video successfully cut and saved.";
                        }
                    } else {
                        msg = g_lastOperationWasExport ? L"Video successfully exported." : L"Video successfully cut and saved.";
                    }
                    title = L"Success";
                    flags = MB_OK | MB_ICONINFORMATION;
                } else if (g_cancelExport) {
                    msg = L"Export canceled.";
                    title = L"Canceled";
                    flags = MB_OK | MB_ICONINFORMATION;
                } else {
                    msg = g_lastOperationWasExport ? L"Failed to export the video." : L"Failed to cut the video.";
                    title = L"Error";
                    flags = MB_OK | MB_ICONERROR;
                }
                MessageBoxW(hwnd, msg, title, flags);
            }
            g_cancelExport = false;
            UpdateControls();
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        if (g_videoPlayer)
        {
            delete g_videoPlayer;
            g_videoPlayer = nullptr;
        }
        if (g_hFont)
        {
            DeleteObject(g_hFont);
            g_hFont = nullptr;
        }
        if (g_hbrBackground)
        {
            DeleteObject(g_hbrBackground);
            g_hbrBackground = nullptr;
        }
        KillTimer(hwnd, 1006); // ID_TIMER_UPDATE
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}
