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
#include <cmath>
#include <cstdlib>
#include <cwchar>

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
void OnPlayClipClicked(HWND hwnd);
void OnPlayEndClipClicked(HWND hwnd);
void OnAudioTrackSelectionChanged();
void UpdateAudioTrackList();
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
extern std::wstring g_lastOutputFile;
extern HBRUSH g_hbrBackground;
extern HFONT g_hFont;
extern COLORREF g_textColor;
extern bool g_isPanelVisible;
extern HWND g_hEditPlaybackSpeed;


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
        case 1029: // ID_BUTTON_TOGGLE_PANEL
            g_isPanelVisible = !g_isPanelVisible;
            RepositionControls(hwnd);
            UpdateControls();
            break;
        case 1030: // ID_BUTTON_SPEED_DOWN
            if (g_videoPlayer && g_videoPlayer->IsLoaded())
            {
                g_videoPlayer->SetPlaybackSpeed(g_videoPlayer->GetPlaybackSpeed() - 0.1);
                UpdateControls();
            }
            break;
        case 1031: // ID_BUTTON_SPEED_UP
            if (g_videoPlayer && g_videoPlayer->IsLoaded())
            {
                g_videoPlayer->SetPlaybackSpeed(g_videoPlayer->GetPlaybackSpeed() + 0.1);
                UpdateControls();
            }
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
        case 1027: // ID_BUTTON_PLAY_CLIP
            OnPlayClipClicked(hwnd);
            break;
        case 1028: // ID_BUTTON_PLAY_END_CLIP
            OnPlayEndClipClicked(hwnd);
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
        case 1026: // ID_CONTEXT_VOICE_ISOLATION
            {
                int selectedIndex = (int)SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
                if (selectedIndex != LB_ERR && g_videoPlayer)
                {
                    bool currentState = g_videoPlayer->IsVoiceIsolationEnabled(selectedIndex);
                    g_videoPlayer->SetVoiceIsolationEnabled(selectedIndex, !currentState);
                    
                    // Update the audio track list display to show the change
                    UpdateAudioTrackList();
                    SendMessage(g_hListBoxAudioTracks, LB_SETCURSEL, selectedIndex, 0);
                    OnAudioTrackSelectionChanged();
                }
            }
            break;
        }

        if ((HWND)lParam == g_hEditPlaybackSpeed && HIWORD(wParam) == EN_KILLFOCUS)
        {
            wchar_t valueText[32];
            GetWindowTextW(g_hEditPlaybackSpeed, valueText, 32);
            for (wchar_t* p = valueText; *p; ++p)
            {
                if (*p == L',')
                    *p = L'.';
            }

            wchar_t* parseEnd = nullptr;
            const double requestedSpeed = std::wcstod(valueText, &parseEnd);
            if (parseEnd != valueText && std::isfinite(requestedSpeed) && g_videoPlayer)
                g_videoPlayer->SetPlaybackSpeed(requestedSpeed);

            if (g_videoPlayer)
            {
                wchar_t normalizedText[32];
                const double speed = g_videoPlayer->GetPlaybackSpeed();
                if (std::fabs(speed - std::round(speed)) < 0.001)
                    swprintf_s(normalizedText, L"%.0fx", speed);
                else
                    swprintf_s(normalizedText, L"%.1fx", speed);
                SetWindowTextW(g_hEditPlaybackSpeed, normalizedText);
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
        {
            HWND targetHwnd = (HWND)wParam;
            if (targetHwnd == g_hListBoxAudioTracks && g_videoPlayer && g_videoPlayer->GetAudioTrackCount() > 0)
            {
                // Get the selected track
                int selectedIndex = (int)SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0);
                if (selectedIndex != LB_ERR)
                {
                    // Create context menu
                    HMENU hMenu = CreatePopupMenu();
                    bool voiceIsolationEnabled = g_videoPlayer->IsVoiceIsolationEnabled(selectedIndex);
                    
                    AppendMenuW(hMenu, MF_STRING | (voiceIsolationEnabled ? MF_CHECKED : MF_UNCHECKED),
                               1026, L"Enable Voice Isolation"); // ID_CONTEXT_VOICE_ISOLATION
                    
                    // Show the menu
                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                    
                    DestroyMenu(hMenu);
                }
            }
        }
        break;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
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
            if (g_videoPlayer)
                g_videoPlayer->UpdatePlaybackSpeedOverlay();
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
    case WM_CTLCOLOREDIT:
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
            } else if (success && !g_autoUpload && (g_useCatbox || g_useB2) && !g_lastOutputFile.empty()) {
                ShowManualUploadDialog(hwnd, g_lastOutputFile, g_useCatbox, g_useB2);
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
