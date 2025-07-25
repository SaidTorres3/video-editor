#include "editing.h"
#include "video_player.h"
#include "ui_updates.h"
#include "progress_window.h"
#include <commdlg.h>
#include <thread>
#include <string>
#include "b2_upload.h"
#include "catbox_upload.h"

// Forward declarations
void UpdateCutInfoLabel(HWND hwnd);
void UpdateCutTimeEdits();

// Global variables
extern VideoPlayer *g_videoPlayer;
extern double g_cutStartTime, g_cutEndTime;
extern HWND g_hStatusText, g_hProgressBar;
extern bool g_useNvenc;
extern bool g_autoUpload;
extern bool g_useCatbox;
extern bool g_useB2;
std::wstring g_uploadedUrl;
bool g_uploadSuccess = false;
bool g_lastOperationWasExport = false;

void OnSetStartClicked(HWND hwnd)
{
    if (!g_videoPlayer || !g_videoPlayer->IsLoaded()) return;
    g_cutStartTime = g_videoPlayer->GetCurrentTime();
    if (g_cutEndTime >= 0 && g_cutStartTime >= g_cutEndTime)
    {
        g_cutEndTime = -1.0; // Invalidate end time if it's before new start time
    }
    UpdateCutInfoLabel(hwnd);
    UpdateCutTimeEdits();
}

void OnSetEndClicked(HWND hwnd)
{
    if (!g_videoPlayer || !g_videoPlayer->IsLoaded()) return;
    double currentTime = g_videoPlayer->GetCurrentTime();
    if (g_cutStartTime >= 0 && currentTime <= g_cutStartTime)
    {
        MessageBoxW(hwnd, L"End point must be after the start point.", L"Invalid Time", MB_OK | MB_ICONWARNING);
        return;
    }
    g_cutEndTime = currentTime;
    UpdateCutInfoLabel(hwnd);
    UpdateCutTimeEdits();
}

void OnCutClicked(HWND hwnd)
{
    g_lastOperationWasExport = false;
    if (!g_videoPlayer || g_cutStartTime < 0 || g_cutEndTime <= g_cutStartTime)
    {
        MessageBoxW(hwnd, L"Please set valid start and end points for the cut.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"MP4 Video\0*.mp4\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"mp4";

    if (GetSaveFileNameW(&ofn))
    {
        SetWindowTextW(g_hStatusText, L"Cutting video... Please wait.");
        EnableWindow(hwnd, FALSE); // Disable UI during cut

        bool mergeAudio = IsDlgButtonChecked(hwnd, 1014) == BST_CHECKED; // ID_CHECKBOX_MERGE_AUDIO
        bool convertH264 = SendMessage(GetDlgItem(hwnd, 1016), BM_GETCHECK, 0, 0) == BST_CHECKED; // ID_RADIO_H264
        wchar_t bitrateText[32];
        GetWindowTextW(GetDlgItem(hwnd, 1017), bitrateText, 32); // ID_EDIT_BITRATE
        int bitrate = _wtoi(bitrateText);

        wchar_t sizeText[32];
        GetWindowTextW(GetDlgItem(hwnd, 1022), sizeText, 32); // ID_EDIT_TARGETSIZE
        int targetSize = _wtoi(sizeText);

        bool useSize = SendMessage(GetDlgItem(hwnd, 1025), BM_GETCHECK, 0, 0) == BST_CHECKED; // ID_RADIO_USE_SIZE

        double startTime = g_cutStartTime;
        double endTime = g_cutEndTime;

        if (convertH264 && useSize && targetSize > 0) {
            double duration = endTime - startTime;
            int audioKbps = 0;
            if (mergeAudio) {
                audioKbps = 128; // single AAC track
            } else {
                for (const auto& track : g_videoPlayer->audioTracks) {
                    if (track->isMuted) continue;
                    AVCodecParameters* par = g_videoPlayer->formatContext->streams[track->streamIndex]->codecpar;
                    int br = par->bit_rate > 0 ? par->bit_rate : 128000;
                    audioKbps += br / 1000;
                }
            }
            int totalKbps = static_cast<int>((targetSize * 8192) / duration);
            bitrate = totalKbps > audioKbps ? (totalKbps - audioKbps) : totalKbps / 2;
        }

        ShowProgressWindow(hwnd);
        std::wstring outFile = szFile;
        std::thread([hwnd, outFile, mergeAudio, convertH264, bitrate, startTime, endTime]() {
            g_uploadSuccess = false;
            g_uploadedUrl.clear();
            bool ok = g_videoPlayer->CutVideo(outFile, startTime, endTime,
                                             mergeAudio, convertH264, g_useNvenc,
                                             bitrate, g_hProgressBar, &g_cancelExport);
            if (ok && g_autoUpload && (g_useCatbox || g_useB2)) {
                std::wstring title = L"Uploading to ";
                title += g_useCatbox ? L"catbox.moe" : L"Backblaze B2";
                SetWindowTextW(g_hProgressWindow, title.c_str());
                std::string url;
                bool up = false;
                if (g_useCatbox)
                    up = UploadToCatbox(outFile, url, g_hProgressBar);
                else if (g_useB2)
                    up = UploadToB2(outFile, url, g_hProgressBar);
                if (up) {
                    int sz = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
                    g_uploadedUrl.assign(sz - 1, 0);
                    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, g_uploadedUrl.data(), sz);
                    g_uploadSuccess = true;
                }
            }
            PostMessage(hwnd, (WM_APP + 1), ok ? 1 : 0, 0); // WM_APP_CUT_DONE
        }).detach();
    }
}

void OnExportClicked(HWND hwnd)
{
    g_lastOperationWasExport = true;
    if (!g_videoPlayer)
        return;

    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"MP4 Video\0*.mp4\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"mp4";

    if (GetSaveFileNameW(&ofn))
    {
        SetWindowTextW(g_hStatusText, L"Exporting video... Please wait.");
        EnableWindow(hwnd, FALSE);

        bool mergeAudio = IsDlgButtonChecked(hwnd, 1014) == BST_CHECKED;
        bool convertH264 = SendMessage(GetDlgItem(hwnd, 1016), BM_GETCHECK, 0, 0) == BST_CHECKED;
        wchar_t bitrateText[32];
        GetWindowTextW(GetDlgItem(hwnd, 1017), bitrateText, 32);
        int bitrate = _wtoi(bitrateText);

        wchar_t sizeText[32];
        GetWindowTextW(GetDlgItem(hwnd, 1022), sizeText, 32);
        int targetSize = _wtoi(sizeText);

        bool useSize = SendMessage(GetDlgItem(hwnd, 1025), BM_GETCHECK, 0, 0) == BST_CHECKED;

        double startTime = 0.0;
        double endTime = g_videoPlayer->GetDuration();

        if (convertH264 && useSize && targetSize > 0) {
            double duration = endTime - startTime;
            int audioKbps = 0;
            if (mergeAudio) {
                audioKbps = 128;
            } else {
                for (const auto& track : g_videoPlayer->audioTracks) {
                    if (track->isMuted) continue;
                    AVCodecParameters* par = g_videoPlayer->formatContext->streams[track->streamIndex]->codecpar;
                    int br = par->bit_rate > 0 ? par->bit_rate : 128000;
                    audioKbps += br / 1000;
                }
            }
            int totalKbps = static_cast<int>((targetSize * 8192) / duration);
            bitrate = totalKbps > audioKbps ? (totalKbps - audioKbps) : totalKbps / 2;
        }

        ShowProgressWindow(hwnd);
        std::wstring outFile = szFile;
        std::thread([hwnd, outFile, mergeAudio, convertH264, bitrate, startTime, endTime]() {
            g_uploadSuccess = false;
            g_uploadedUrl.clear();
            bool ok = g_videoPlayer->CutVideo(outFile, startTime, endTime,
                                             mergeAudio, convertH264, g_useNvenc,
                                             bitrate, g_hProgressBar, &g_cancelExport);
            if (ok && g_autoUpload && (g_useCatbox || g_useB2)) {
                std::wstring title = L"Uploading to ";
                title += g_useCatbox ? L"catbox.moe" : L"Backblaze B2";
                SetWindowTextW(g_hProgressWindow, title.c_str());
                std::string url;
                bool up = false;
                if (g_useCatbox)
                    up = UploadToCatbox(outFile, url, g_hProgressBar);
                else if (g_useB2)
                    up = UploadToB2(outFile, url, g_hProgressBar);
                if (up) {
                    int sz = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
                    g_uploadedUrl.assign(sz - 1, 0);
                    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, g_uploadedUrl.data(), sz);
                    g_uploadSuccess = true;
                }
            }
            PostMessage(hwnd, (WM_APP + 1), ok ? 1 : 0, 0);
        }).detach();
    }
}
